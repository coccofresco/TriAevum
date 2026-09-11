#include "fast/renderer3ds/pica_surface_lighting_pass.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"

#ifdef ENABLE_RENDERER3DS_VULKAN
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <stdexcept>

namespace Fast::Renderer3ds {
namespace {
constexpr uint32_t kWidth = 256, kHeight = 1024, kSlots = 2, kSets = 256;
void Check(VkResult value) {
    if (value != VK_SUCCESS) throw std::runtime_error("native surface lighting Vulkan resource failure");
}
uint32_t MemoryType(VkPhysicalDevice physical, uint32_t mask) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((mask & (1U << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
    throw std::runtime_error("native surface lighting device-local memory unavailable");
}
}
struct PicaSurfaceLightingPass::Impl {
    struct Slot {
        VkImage Image{};
        VkDeviceMemory Memory{};
        VkImageView View{};
        VkFramebuffer Framebuffer{};
        std::array<VkDescriptorSet, kSets> Sets{};
        uint64_t FrameId = ~uint64_t{};
        uint32_t Used = 0;
        bool Initialized = false;
    };
    VkPhysicalDevice Physical{};
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkDevice Device{};
    Renderer::CachedPassShaderCompiler* Shaders = nullptr;
    VkDescriptorSetLayout DescriptorLayout{};
    VkDescriptorPool Pool{};
    VkPipelineLayout Layout{};
    VkRenderPass RenderPass{};
    VkSampler Sampler{};
    std::array<Slot, kSlots> Slots;
    std::map<std::pair<uint64_t, uint64_t>, VkPipeline> Pipelines;

    VkShaderModule Compile(std::string_view source, Renderer::SpirvStage stage, const char* name) {
        auto words = Shaders->Resolve(source, stage, name);
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = words.size() * sizeof(uint32_t);
        info.pCode = words.data();
        VkShaderModule module{};
        Check(vkCreateShaderModule(Device, &info, nullptr, &module));
        return module;
    }
    VkPipeline Pipeline(const PicaResolvedDrawRecord& draw) {
        if (!draw.VertexShader || !draw.VertexLayout || !draw.VertexShader->VertexHooksAvailable()) return {};
        const auto key = std::pair{draw.VertexShader->Key, draw.VertexLayout->StructuralKey};
        if (auto found = Pipelines.find(key); found != Pipelines.end()) return found->second;
        auto source = BuildPicaSurfaceLightingVertexShader(draw.VertexShader->Source, draw.VertexShader->VertexHooks);
        if (source.empty()) return {};
        VkShaderModule vertex{}, fragment{};
        VkPipeline result{};
        try {
            vertex = Compile(source, Renderer::SpirvStage::Vertex, "native_surface_lighting.vert");
            fragment = Compile(kPicaSurfaceLightingFragmentShader, Renderer::SpirvStage::Fragment, "native_surface_lighting.frag");
            std::vector<VkVertexInputBindingDescription> bindings;
            std::vector<VkVertexInputAttributeDescription> attributes;
            for (const auto& b : draw.VertexLayout->Bindings)
                bindings.push_back({b.Binding, b.ByteStride, b.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
            for (const auto& a : draw.VertexLayout->Attributes)
                attributes.push_back({a.Location, a.Binding, VK_FORMAT_R32G32B32A32_SFLOAT, a.ByteOffset});
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            input.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size()); input.pVertexBindingDescriptions = bindings.data();
            input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0F;
            VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkViewport viewport{0, 0, float(kWidth), float(kHeight), 0, 1};
            VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
            VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewportState.viewportCount = viewportState.scissorCount = 1;
            viewportState.pViewports = &viewport; viewportState.pScissors = &scissor;
            VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount = 1; blend.pAttachments = &attachment;
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            for (size_t i = 0; i < stages.size(); ++i) {
                stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
                stages[i].module = i ? fragment : vertex; stages[i].pName = "main";
            }
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2; info.pStages = stages.data(); info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly; info.pRasterizationState = &raster;
            info.pMultisampleState = &samples; info.pViewportState = &viewportState;
            info.pColorBlendState = &blend; info.layout = Layout; info.renderPass = RenderPass;
            Check(vkCreateGraphicsPipelines(Device, {}, 1, &info, nullptr, &result));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Native surface lighting: %s\n", e.what());
        }
        if (vertex) vkDestroyShaderModule(Device, vertex, nullptr);
        if (fragment) vkDestroyShaderModule(Device, fragment, nullptr);
        Pipelines.emplace(key, result);
        return result;
    }
};

PicaSurfaceLightingPass::PicaSurfaceLightingPass() : mImpl(std::make_unique<Impl>()) {}
PicaSurfaceLightingPass::~PicaSurfaceLightingPass() { Shutdown(); }
bool PicaSurfaceLightingPass::Initialize(VkPhysicalDevice physical, VkDevice device, Renderer::CachedPassShaderCompiler& shaders) {
    Shutdown();
    auto& p = *mImpl; p.Physical = physical; p.Device = device; p.Shaders = &shaders;
    try {
        constexpr auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        for (auto format : {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &props);
            if ((props.optimalTilingFeatures & required) == required) { p.Format = format; break; }
        }
        if (p.Format == VK_FORMAT_UNDEFINED) throw std::runtime_error("surface floating-point atlas unsupported");
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = 1; dl.pBindings = &binding; Check(vkCreateDescriptorSetLayout(device, &dl, nullptr, &p.DescriptorLayout));
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1; pl.pSetLayouts = &p.DescriptorLayout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(device, &pl, nullptr, &p.Layout));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets * kSlots};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = size.descriptorCount; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
        Check(vkCreateDescriptorPool(device, &pool, nullptr, &p.Pool));
        VkAttachmentDescription attachment{}; attachment.format = p.Format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &subpass;
        Check(vkCreateRenderPass(device, &rp, nullptr, &p.RenderPass));
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Check(vkCreateSampler(device, &sampler, nullptr, &p.Sampler));
        for (auto& slot : p.Slots) {
            std::array<VkDescriptorSetLayout, kSets> layouts; layouts.fill(p.DescriptorLayout);
            VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc.descriptorPool = p.Pool; alloc.descriptorSetCount = kSets; alloc.pSetLayouts = layouts.data();
            Check(vkAllocateDescriptorSets(device, &alloc, slot.Sets.data()));
            VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            image.imageType = VK_IMAGE_TYPE_2D; image.format = p.Format; image.extent = {kWidth, kHeight, 1};
            image.mipLevels = image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            Check(vkCreateImage(device, &image, nullptr, &slot.Image));
            VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, slot.Image, &req);
            VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            memory.allocationSize = req.size; memory.memoryTypeIndex = MemoryType(physical, req.memoryTypeBits);
            Check(vkAllocateMemory(device, &memory, nullptr, &slot.Memory)); Check(vkBindImageMemory(device, slot.Image, slot.Memory, 0));
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = slot.Image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = p.Format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            Check(vkCreateImageView(device, &view, nullptr, &slot.View));
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = p.RenderPass; fb.attachmentCount = 1; fb.pAttachments = &slot.View;
            fb.width = kWidth; fb.height = kHeight; fb.layers = 1;
            Check(vkCreateFramebuffer(device, &fb, nullptr, &slot.Framebuffer));
        }
        return true;
    } catch (const std::exception& e) { std::fprintf(stderr, "Native surface lighting initialization: %s\n", e.what()); Shutdown(); return false; }
}

bool PicaSurfaceLightingPass::Prepare(VkCommandBuffer command, uint32_t frameSlot, uint64_t frameId,
    PicaResolvedDrawStreamView scene, std::span<PicaSurfaceLightingRequest> requests) {
    auto& p = *mImpl;
    if (!p.Device) return false;
    auto& slot = p.Slots[frameSlot % kSlots];
    if (slot.FrameId != frameId) { slot.FrameId = frameId; slot.Used = 0; }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = slot.Initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = slot.Initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = slot.Image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, slot.Initialized ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = p.RenderPass; begin.framebuffer = slot.Framebuffer; begin.renderArea.extent = {kWidth, kHeight};
    begin.clearValueCount = 1; begin.pClearValues = &clear;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    uint32_t base = 0;
    for (auto& request : requests) {
        request.Available = false;
        if (!request.VertexCount || request.VertexCount > kWidth * kHeight - base || slot.Used == kSets) continue;
        auto found = std::find_if(scene.Draws.begin(), scene.Draws.end(), [&](const auto& d) {
            return d.SubmissionId == request.SubmissionId &&
                d.RenderTarget.RenderTargetNamespace == request.RenderTargetNamespace;
        });
        if (found == scene.Draws.end() || found->CompositionDomain != PicaCompositionDomain::Scene ||
            found->Material.FragmentFeatures.FragmentLightingEnabled) continue;
        const auto& draw = *found;
        if (!draw.VertexLayout) continue;
        bool valid = draw.UniformBuffer.NativeHandle && draw.GeometryBuffer.NativeHandle &&
            draw.VertexUniformSize && draw.VertexUniformOffset <= draw.UniformBuffer.Size &&
            draw.VertexUniformSize <= draw.UniformBuffer.Size - draw.VertexUniformOffset;
        const auto bindings = scene.BindingsFor(draw);
        valid &= !bindings.empty();
        for (const auto& b : bindings) {
            const auto layout = std::find_if(draw.VertexLayout->Bindings.begin(), draw.VertexLayout->Bindings.end(),
                [&](const auto& l) { return l.Binding == b.Binding; });
            if (layout == draw.VertexLayout->Bindings.end() || layout->ByteStride != b.Stride) { valid = false; break; }
            const uint64_t count = layout != draw.VertexLayout->Bindings.end() && layout->PerInstance ? 1 : request.VertexCount;
            valid &= b.Stride && b.Offset <= draw.GeometryBuffer.Size && b.Size <= draw.GeometryBuffer.Size - b.Offset && count <= b.Size / b.Stride;
        }
        if (!valid) continue;
        const auto pipeline = p.Pipeline(draw); if (!pipeline) continue;
        VkDescriptorBufferInfo uniform{reinterpret_cast<VkBuffer>(draw.UniformBuffer.NativeHandle), draw.VertexUniformOffset, draw.VertexUniformSize};
        auto set = slot.Sets[slot.Used++];
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &uniform; vkUpdateDescriptorSets(p.Device, 1, &write, 0, nullptr);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.Layout, 0, 1, &set, 0, nullptr);
        VkBuffer geometry = reinterpret_cast<VkBuffer>(draw.GeometryBuffer.NativeHandle);
        for (const auto& b : bindings) { VkDeviceSize offset = b.Offset; vkCmdBindVertexBuffers(command, b.Binding, 1, &geometry, &offset); }
        std::array<uint32_t, 4> push{base, kWidth, kHeight, 0};
        vkCmdPushConstants(command, p.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, push.data());
        vkCmdDraw(command, request.VertexCount, 1, 0, 0);
        request.AtlasBase = base; request.Available = true; base += request.VertexCount;
    }
    vkCmdEndRenderPass(command);
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    slot.Initialized = true;
    return true;
}
VkImageView PicaSurfaceLightingPass::View(uint32_t frameSlot) const { return mImpl->Slots[frameSlot % kSlots].View; }
VkSampler PicaSurfaceLightingPass::Sampler() const { return mImpl->Sampler; }
void PicaSurfaceLightingPass::Shutdown() {
    auto& p = *mImpl;
    if (p.Device) {
        for (auto& [key, pipeline] : p.Pipelines) if (pipeline) vkDestroyPipeline(p.Device, pipeline, nullptr);
        for (auto& slot : p.Slots) {
            if (slot.Framebuffer) vkDestroyFramebuffer(p.Device, slot.Framebuffer, nullptr);
            if (slot.View) vkDestroyImageView(p.Device, slot.View, nullptr);
            if (slot.Image) vkDestroyImage(p.Device, slot.Image, nullptr);
            if (slot.Memory) vkFreeMemory(p.Device, slot.Memory, nullptr);
        }
        if (p.Sampler) vkDestroySampler(p.Device, p.Sampler, nullptr);
        if (p.RenderPass) vkDestroyRenderPass(p.Device, p.RenderPass, nullptr);
        if (p.Layout) vkDestroyPipelineLayout(p.Device, p.Layout, nullptr);
        if (p.Pool) vkDestroyDescriptorPool(p.Device, p.Pool, nullptr);
        if (p.DescriptorLayout) vkDestroyDescriptorSetLayout(p.Device, p.DescriptorLayout, nullptr);
    }
    mImpl = std::make_unique<Impl>();
}
} // namespace Fast::Renderer3ds
#endif
