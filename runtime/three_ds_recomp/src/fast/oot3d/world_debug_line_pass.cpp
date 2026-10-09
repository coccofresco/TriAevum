#include "fast/oot3d/world_debug_line_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {

namespace {

constexpr const char* kVertexShader = R"glsl(#version 450
layout(location = 0) in vec4 clip_from;
layout(location = 1) in vec4 clip_to;
layout(location = 2) in vec4 color;
layout(location = 3) in vec2 corner;
layout(push_constant) uniform LineState {
    // Target width and height, half the line width (pixels), hidden pass.
    vec4 viewport;
    // Hidden opacity, dash length (pixels).
    vec4 style;
} lines;
layout(location = 0) out vec4 line_color;
layout(location = 1) noperspective out float line_distance;
void main() {
    vec2 half_extent = 0.5 * lines.viewport.xy;
    vec2 from_pixels = clip_from.xy / clip_from.w * half_extent;
    vec2 to_pixels = clip_to.xy / clip_to.w * half_extent;
    vec2 delta = to_pixels - from_pixels;
    float length_pixels = length(delta);
    vec2 along = length_pixels > 1.0e-4 ? delta / length_pixels : vec2(1.0, 0.0);
    vec2 across = vec2(-along.y, along.x);
    bool at_to = corner.x > 0.5;
    vec4 clip = at_to ? clip_to : clip_from;
    // Square caps: half the width past each end.
    vec2 offset = (across * corner.y + along * (at_to ? 1.0 : -1.0)) * lines.viewport.z;
    clip.xy += offset / half_extent * clip.w;
    gl_Position = clip;
    line_color = color;
    line_distance = at_to ? length_pixels : 0.0;
}
)glsl";

constexpr const char* kFragmentShader = R"glsl(#version 450
layout(push_constant) uniform LineState {
    vec4 viewport;
    vec4 style;
} lines;
layout(location = 0) in vec4 line_color;
layout(location = 1) noperspective in float line_distance;
layout(location = 0) out vec4 out_color;
void main() {
    float alpha = line_color.a;
    if (lines.viewport.w > 0.5) {
        if (mod(line_distance, 2.0 * lines.style.y) > lines.style.y) discard;
        alpha *= lines.style.x;
    }
    out_color = vec4(line_color.rgb, alpha);
}
)glsl";

struct LinePushConstants {
    std::array<float, 4> Viewport{};
    std::array<float, 4> Style{};
};

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t mask, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        if ((mask & (1U << index)) != 0U && (properties.memoryTypes[index].propertyFlags & required) == required)
            return index;
    throw std::runtime_error("world debug lines have no host-visible memory type");
}

VkShaderModule Compile(Renderer::CachedPassShaderCompiler& shaders, VkDevice device, const char* source,
                       Renderer::SpirvStage stage, const char* name) {
    const auto words = shaders.Resolve(source, stage, name);
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error(std::string("cannot create ") + name);
    return module;
}

} // namespace

struct WorldDebugLinePass::Impl {
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    Renderer::CachedPassShaderCompiler* Shaders = nullptr;
    VkRenderPass CanonicalRenderPass = VK_NULL_HANDLE;
    VkRenderPass InstrumentedRenderPass = VK_NULL_HANDLE;
    VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool DynamicRendering = false;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkPipelineCache PipelineCache = VK_NULL_HANDLE;
    bool Initialized = false;
    bool PipelinesBuilt = false;
    std::string UnavailableReason;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    // [attachment layout 0..2][visible, hidden]
    std::array<std::array<VkPipeline, 2>, 3> Pipelines{};
    struct BufferSlot {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0;
        void* Mapped = nullptr;
        // Draws of one frame append; the next frame in this slot starts over.
        uint64_t FrameId = 0;
        VkDeviceSize Used = 0;
    };
    std::vector<BufferSlot> Buffers;

    void DestroyBuffer(BufferSlot& slot) {
        if (slot.Mapped != nullptr) vkUnmapMemory(Device, slot.Memory);
        if (slot.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, slot.Buffer, nullptr);
        if (slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Device, slot.Memory, nullptr);
        slot = {};
    }

    void EnsureBuffer(BufferSlot& slot, VkDeviceSize bytes) {
        if (bytes <= slot.Capacity) return;
        DestroyBuffer(slot);
        slot.Capacity = std::bit_ceil(std::max<VkDeviceSize>(bytes, 64U * 1024U));
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = slot.Capacity;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &bufferInfo, nullptr, &slot.Buffer) != VK_SUCCESS)
            throw std::runtime_error("cannot create the world debug line buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, slot.Buffer, &requirements);
        VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(
            PhysicalDevice, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(Device, &allocation, nullptr, &slot.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(Device, slot.Buffer, slot.Memory, 0) != VK_SUCCESS ||
            vkMapMemory(Device, slot.Memory, 0, slot.Capacity, 0, &slot.Mapped) != VK_SUCCESS)
            throw std::runtime_error("cannot map the world debug line buffer");
    }

    void DestroyPipelines() {
        for (auto& pair : Pipelines)
            for (auto& pipeline : pair) {
                if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        if (PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        PipelineLayout = VK_NULL_HANDLE;
        PipelinesBuilt = false;
    }

    void BuildPipelines() {
        VkPushConstantRange push{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                  sizeof(LinePushConstants) };
        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1U;
        layoutInfo.pPushConstantRanges = &push;
        if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("cannot create the world debug line pipeline layout");
        const VkShaderModule vertex =
            Compile(*Shaders, Device, kVertexShader, Renderer::SpirvStage::Vertex, "oot3d_world_debug_lines.vert");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = Compile(*Shaders, Device, kFragmentShader, Renderer::SpirvStage::Fragment,
                               "oot3d_world_debug_lines.frag");
            const VkPipelineShaderStageCreateInfo stages[] = {
                { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                  vertex, "main", nullptr },
                { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                  fragment, "main", nullptr } };
            const VkVertexInputBindingDescription binding{ 0, sizeof(WorldDebugLineVertex),
                                                           VK_VERTEX_INPUT_RATE_VERTEX };
            const VkVertexInputAttributeDescription attributes[] = {
                { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(WorldDebugLineVertex, ClipFrom) },
                { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(WorldDebugLineVertex, ClipTo) },
                { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(WorldDebugLineVertex, Color) },
                { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(WorldDebugLineVertex, Corner) } };
            VkPipelineVertexInputStateCreateInfo vertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInput.vertexBindingDescriptionCount = 1U;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
            vertexInput.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            viewport.viewportCount = 1U;
            viewport.scissorCount = 1U;
            VkPipelineRasterizationStateCreateInfo raster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0F;
            VkPipelineMultisampleStateCreateInfo multisample{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            multisample.rasterizationSamples = SampleCount;
            // The scene's depth stays as it is: the lines only test against it.
            VkPipelineDepthStencilStateCreateInfo depth{
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_FALSE;
            // Colour only: the guides (normals, motion, outlines) keep the scene's.
            VkPipelineColorBlendAttachmentState colorAttachment{};
            colorAttachment.blendEnable = VK_TRUE;
            colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            std::array<VkPipelineColorBlendAttachmentState, kPicaColorAttachmentCount> attachments{};
            attachments[0] = colorAttachment;
            VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            blend.pAttachments = attachments.data();
            const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            dynamic.dynamicStateCount = 2U;
            dynamic.pDynamicStates = dynamicStates;
            VkGraphicsPipelineCreateInfo pipeline{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            pipeline.stageCount = 2U;
            pipeline.pStages = stages;
            pipeline.pVertexInputState = &vertexInput;
            pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport;
            pipeline.pRasterizationState = &raster;
            pipeline.pMultisampleState = &multisample;
            pipeline.pDepthStencilState = &depth;
            pipeline.pColorBlendState = &blend;
            pipeline.pDynamicState = &dynamic;
            pipeline.layout = PipelineLayout;
            // The scene geometry passes' attachment formats (interactive grass).
            const std::array<VkFormat, kPicaColorAttachmentCount> colorFormats{
                VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_R32G32B32A32_UINT,   VK_FORMAT_R16G16B16A16_UNORM };
            VkPipelineRenderingCreateInfoKHR rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
            if (DynamicRendering) {
                if (DepthFormat == VK_FORMAT_UNDEFINED)
                    throw std::runtime_error("world debug lines: dynamic depth format is invalid");
                rendering.pColorAttachmentFormats = colorFormats.data();
                rendering.depthAttachmentFormat = DepthFormat;
                if (DepthFormat == VK_FORMAT_D16_UNORM_S8_UINT || DepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
                    DepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
                    rendering.stencilAttachmentFormat = DepthFormat;
                pipeline.pNext = &rendering;
                pipeline.renderPass = VK_NULL_HANDLE;
            }
            const std::array<uint32_t, 3> attachmentCounts{
                1U, static_cast<uint32_t>(kPicaBaseInstrumentedColorAttachmentCount),
                static_cast<uint32_t>(kPicaColorAttachmentCount) };
            const std::array<VkRenderPass, 2> renderPasses{ CanonicalRenderPass, InstrumentedRenderPass };
            for (size_t layout = 0; layout < Pipelines.size(); ++layout) {
                if (layout == 2U && !DynamicRendering) continue;
                blend.attachmentCount = attachmentCounts[layout];
                if (DynamicRendering) {
                    rendering.colorAttachmentCount = attachmentCounts[layout];
                } else {
                    if (renderPasses[layout] == VK_NULL_HANDLE)
                        throw std::runtime_error("world debug lines: render pass is unavailable");
                    pipeline.renderPass = renderPasses[layout];
                }
                for (size_t hidden = 0; hidden < 2U; ++hidden) {
                    // Visible: in front of the scene; hidden: behind it.
                    depth.depthCompareOp = hidden == 0U ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_GREATER;
                    if (vkCreateGraphicsPipelines(Device, PipelineCache, 1U, &pipeline, nullptr,
                                                  &Pipelines[layout][hidden]) != VK_SUCCESS)
                        throw std::runtime_error("cannot create a world debug line pipeline");
                }
            }
        } catch (...) {
            if (fragment != VK_NULL_HANDLE) vkDestroyShaderModule(Device, fragment, nullptr);
            vkDestroyShaderModule(Device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(Device, fragment, nullptr);
        vkDestroyShaderModule(Device, vertex, nullptr);
        PipelinesBuilt = true;
    }
};

WorldDebugLinePass::WorldDebugLinePass() : mImpl(std::make_unique<Impl>()) {}
WorldDebugLinePass::~WorldDebugLinePass() { Shutdown(); }

void WorldDebugLinePass::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                    Renderer::CachedPassShaderCompiler& shaders,
                                    VkRenderPass canonicalRenderPass, VkRenderPass instrumentedRenderPass,
                                    VkSampleCountFlagBits sampleCount, bool dynamicRendering,
                                    VkFormat depthFormat, VkPipelineCache pipelineCache) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    mImpl->Shaders = &shaders;
    mImpl->CanonicalRenderPass = canonicalRenderPass;
    mImpl->InstrumentedRenderPass = instrumentedRenderPass;
    mImpl->SampleCount = sampleCount;
    mImpl->DynamicRendering = dynamicRendering;
    mImpl->DepthFormat = depthFormat;
    mImpl->PipelineCache = pipelineCache;
    mImpl->UnavailableReason.clear();
    mImpl->Initialized = device != VK_NULL_HANDLE;
}

bool WorldDebugLinePass::Draw(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId,
                              uint32_t width, uint32_t height, const WorldDebugLineSet& lines,
                              const std::array<float, 16>& worldToClip, const std::array<float, 2>& jitterNdc,
                              const PicaAttachmentRequirements& attachments) {
    auto& impl = *mImpl;
    if (!impl.Initialized || !impl.UnavailableReason.empty() || commandBuffer == VK_NULL_HANDLE ||
        width == 0U || height == 0U || lines.Lines.empty())
        return false;
    try {
        if (!impl.PipelinesBuilt) impl.BuildPipelines();
        const size_t layout = attachments.NativeColorOnly() ? 0U
            : attachments.Requires(PicaAuxiliaryOutput::DiffuseAlbedoGuide) ? 2U : 1U;
        const auto& pipelines = impl.Pipelines[layout];
        if (pipelines[0] == VK_NULL_HANDLE || pipelines[1] == VK_NULL_HANDLE) return false;
        const auto vertices = BuildWorldDebugLineVertices(lines, worldToClip, jitterNdc);
        if (vertices.empty()) return false;
        if (impl.Buffers.size() <= frameSlot) impl.Buffers.resize(frameSlot + 1U);
        auto& slot = impl.Buffers[frameSlot];
        if (slot.FrameId != frameId) {
            slot.FrameId = frameId;
            slot.Used = 0U;
        }
        const VkDeviceSize bytes = vertices.size() * sizeof(WorldDebugLineVertex);
        // This frame's earlier draws still read the slot's buffer: it grows
        // only between frames, a later draw that does not fit is skipped.
        if (slot.Used != 0U && slot.Used + bytes > slot.Capacity) return false;
        impl.EnsureBuffer(slot, slot.Used + bytes);
        const VkDeviceSize offset = slot.Used;
        std::memcpy(static_cast<std::byte*>(slot.Mapped) + offset, vertices.data(), bytes);
        slot.Used += bytes;

        // Widths are given at 720 lines; the physical target is transposed,
        // so its shorter side is the logical height.
        const float scale = static_cast<float>(std::min(width, height)) / 720.0F;
        LinePushConstants push;
        push.Viewport = { static_cast<float>(width), static_cast<float>(height),
                          std::max(0.5F, 0.5F * lines.WidthPixels * scale), 0.0F };
        push.Style = { std::clamp(lines.HiddenOpacity, 0.0F, 1.0F), std::max(2.0F, 6.0F * scale), 0.0F, 0.0F };
        const VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F };
        const VkRect2D scissor{ { 0, 0 }, { width, height } };
        vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
        vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);
        vkCmdBindVertexBuffers(commandBuffer, 0U, 1U, &slot.Buffer, &offset);
        const auto count = static_cast<uint32_t>(vertices.size());
        // Hidden parts first, so the visible ones draw over them.
        for (const size_t hidden : { size_t{ 1U }, size_t{ 0U } }) {
            push.Viewport[3] = hidden == 1U ? 1.0F : 0.0F;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[hidden]);
            vkCmdPushConstants(commandBuffer, impl.PipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(push), &push);
            vkCmdDraw(commandBuffer, count, 1U, 0U, 0U);
        }
        return true;
    } catch (const std::exception& exception) {
        impl.UnavailableReason = exception.what();
        return false;
    }
}

void WorldDebugLinePass::Shutdown() {
    if (!mImpl) return;
    auto& impl = *mImpl;
    if (impl.Device != VK_NULL_HANDLE) {
        impl.DestroyPipelines();
        for (auto& slot : impl.Buffers) impl.DestroyBuffer(slot);
    }
    impl.Buffers.clear();
    impl.Initialized = false;
}

const std::string& WorldDebugLinePass::UnavailableReason() const {
    return mImpl->UnavailableReason;
}

} // namespace Fast::Oot3d

#endif
