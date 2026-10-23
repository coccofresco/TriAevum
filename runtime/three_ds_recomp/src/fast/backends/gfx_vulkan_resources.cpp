// Vulkan backend: frame resources, Shadow2D targets, buffers, texture images, descriptor sets
// and graphics pipelines.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"

namespace Fast {

void GfxRenderingAPIVulkan::EnsureFrameUniformCapacity(
    VkDeviceSize bytes, VkDeviceSize alignment) {
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize offset =
        (frame.UniformBytesUsed + alignment - 1U) & ~(alignment - 1U);
    if (offset <= frame.UniformBuffer.Size &&
        bytes <= frame.UniformBuffer.Size - offset) {
        return;
    }
    // Catch-up refreshes share one submitted frame. Reserve a complete draw
    // before uploading any of its blocks, and retain buffers referenced by
    // already-recorded draws until this frame's fence completes.
    frame.RetiredUniformBuffers.reserve(frame.RetiredUniformBuffers.size() + 1U);
    const auto replacement = CreateBuffer(
        std::max(frame.UniformBuffer.Size * 2U, bytes),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    frame.RetiredUniformBuffers.push_back(frame.UniformBuffer);
    frame.UniformBuffer = replacement;
    frame.UniformBytesUsed = 0;
    frame.SurfaceLightingUniformOffset.reset();
    // Legacy descriptor keys contain textures, not the implicit uniform buffer.
    // Existing sets stay alive in the frame pool; new draws need fresh bindings.
    frame.TextureDescriptorSets.clear();
}

VkDeviceSize GfxRenderingAPIVulkan::EnsureFrameVertexCapacity(VkDeviceSize bytes) {
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize offset = (frame.VertexBytesUsed + 15U) & ~VkDeviceSize(15U);
    if (offset <= frame.VertexBuffer.Size && bytes <= frame.VertexBuffer.Size - offset) {
        return offset;
    }
    // A scene that outgrows the arena (heavy transient geometry, large texture
    // uploads in one frame) continues on a larger buffer instead of ending the
    // session. Draws already recorded keep the old buffer until the fence.
    // Adapted from the WINDROID fork's overflow fallback (c24df3a).
    frame.RetiredVertexBuffers.reserve(frame.RetiredVertexBuffers.size() + 1U);
    const VkDeviceSize grown = std::max(frame.VertexBuffer.Size * 2U, bytes + 16U);
    const auto replacement = CreateBuffer(
        grown,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    std::fprintf(stderr, "TRIAEVUM_VULKAN_VERTEX_ARENA_GROWN frame=%llu slot=%u bytes=%llu->%llu request=%llu\n",
                 static_cast<unsigned long long>(mFrameCounter), mCurrentFrame,
                 static_cast<unsigned long long>(frame.VertexBuffer.Size), static_cast<unsigned long long>(grown),
                 static_cast<unsigned long long>(bytes));
    SPDLOG_INFO("OOT3D Vulkan per-frame vertex arena grown {} -> {} bytes (request {})", frame.VertexBuffer.Size,
                grown, bytes);
    ++mFrameVertexArenaGrowths;
    frame.RetiredVertexBuffers.push_back(frame.VertexBuffer);
    frame.VertexBuffer = replacement;
    frame.VertexBytesUsed = 0;
    return 0;
}

VkDeviceSize GfxRenderingAPIVulkan::InitialFrameVertexArenaBytes() {
    constexpr VkDeviceSize kDefault = 64ull * 1024ull * 1024ull;
    // Diagnostic only: a deliberately small arena exercises the growth path.
    if (const char* value = std::getenv("TRIAEVUM_VULKAN_VERTEX_ARENA_KIB"); value != nullptr && *value != '\0') {
        char* end = nullptr;
        const unsigned long kib = std::strtoul(value, &end, 10);
        if (end != nullptr && *end == '\0' && kib >= 16UL && kib <= 1048576UL) {
            return static_cast<VkDeviceSize>(kib) * 1024ull;
        }
        SPDLOG_WARN("TRIAEVUM_VULKAN_VERTEX_ARENA_KIB ignored: expected 16..1048576, got '{}'", value);
    }
    return kDefault;
}

void GfxRenderingAPIVulkan::CreateFrameResources() {
    const VkDeviceSize kVertexArenaSize = InitialFrameVertexArenaBytes();
    constexpr VkDeviceSize kUniformArenaSize = 4ull * 1024ull * 1024ull;
    for (auto& frame : mFrameResources) {
        frame.VertexBuffer =
            CreateBuffer(kVertexArenaSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         true);
        frame.UniformBuffer =
            CreateBuffer(kUniformArenaSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         true);
        const std::array<VkDescriptorPoolSize, 5> poolSizes = {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 * 9 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 4096 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096 * 7 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096 },
        };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        CheckVk(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &frame.DescriptorPool),
                "vkCreateDescriptorPool");
    }
}

void GfxRenderingAPIVulkan::CreateFallbackTexture() {
    constexpr std::array<uint8_t, 4> white = { 255, 255, 255, 255 };
    CreateTextureImage(mFallbackTexture, white.data(), 1, 1);

    TextureRecord& guide = mGeometryGuideFallback;
    guide.Width = 1;
    guide.Height = 1;
    guide.MipLevels = 1;
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32B32A32_UINT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &guide.Image), "vkCreateImage(geometry guide fallback)");
    guide.NriImage = guide.Image;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(mDevice, guide.Image, &requirements);
    VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &guide.Memory),
            "vkAllocateMemory(geometry guide fallback)");
    CheckVk(vkBindImageMemory(mDevice, guide.Image, guide.Memory, 0), "vkBindImageMemory(geometry guide fallback)");
    TransitionImageLayout(guide.Image, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer clearCommand = BeginImmediateCommands();
    VkClearColorValue cleared{};
    for (size_t channel = 0; channel < 4; ++channel)
        cleared.uint32[channel] = Fast::Renderer3ds::kPicaGeometryGuideClear[channel];
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(clearCommand, guide.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cleared, 1, &range);
    EndImmediateCommands(clearCommand);
    TransitionImageLayout(guide.Image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = guide.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_UINT;
    viewInfo.subresourceRange = range;
    CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &guide.View),
            "vkCreateImageView(geometry guide fallback)");
    guide.ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    guide.Uploaded = true;
}

void GfxRenderingAPIVulkan::CreateOot3dShadow2dRenderPass() {
    if (mOot3dShadow2dRenderPass != VK_NULL_HANDLE) {
        return;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32_UINT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = mDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment, depthAttachment
    };
    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    CheckVk(vkCreateRenderPass(mDevice, &renderPassInfo, nullptr,
                               &mOot3dShadow2dRenderPass),
            "vkCreateRenderPass(OOT3D Shadow2D)");
}

void GfxRenderingAPIVulkan::DestroyOffscreenFramebuffer(
    OffscreenFramebufferRecord& framebuffer) {
    if (mDevice == VK_NULL_HANDLE) {
        framebuffer = {};
        return;
    }
    if (framebuffer.Framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(mDevice, framebuffer.Framebuffer, nullptr);
    }
    if (framebuffer.ColorSampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, framebuffer.ColorSampler, nullptr);
    }
    if (framebuffer.ColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, framebuffer.ColorView, nullptr);
    }
    if (framebuffer.ColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, framebuffer.ColorImage, nullptr);
    }
    if (framebuffer.ColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, framebuffer.ColorMemory, nullptr);
    }
    if (framebuffer.DepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, framebuffer.DepthView, nullptr);
    }
    if (framebuffer.DepthImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice, framebuffer.DepthImage, nullptr);
    }
    if (framebuffer.DepthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, framebuffer.DepthMemory, nullptr);
    }
    framebuffer = {};
}

bool GfxRenderingAPIVulkan::CreateOot3dShadow2dFramebuffer(
    OffscreenFramebufferRecord& framebuffer, uint32_t width, uint32_t height,
    bool hasDepthBuffer) {
    if (!hasDepthBuffer || !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    CreateOot3dShadow2dRenderPass();

    const auto createImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image,
                                 VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &image),
                "vkCreateImage(OOT3D Shadow2D)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, image, &requirements);
        VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex =
            FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &memory),
                "vkAllocateMemory(OOT3D Shadow2D)");
        CheckVk(vkBindImageMemory(mDevice, image, memory, 0),
                "vkBindImageMemory(OOT3D Shadow2D)");
    };

    createImage(VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                framebuffer.ColorImage, framebuffer.ColorMemory);
    VkImageViewCreateInfo colorViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    colorViewInfo.image = framebuffer.ColorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = VK_FORMAT_R32_UINT;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &colorViewInfo, nullptr, &framebuffer.ColorView),
            "vkCreateImageView(OOT3D Shadow2D color)");

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    CheckVk(vkCreateSampler(mDevice, &samplerInfo, nullptr, &framebuffer.ColorSampler),
            "vkCreateSampler(OOT3D Shadow2D)");

    TransitionImageLayout(framebuffer.ColorImage, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer clearCommand = BeginImmediateCommands();
    VkClearColorValue initialClear{};
    initialClear.uint32[0] = UINT32_MAX;
    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.levelCount = 1;
    colorRange.layerCount = 1;
    vkCmdClearColorImage(clearCommand, framebuffer.ColorImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &initialClear, 1,
                         &colorRange);
    EndImmediateCommands(clearCommand);
    TransitionImageLayout(framebuffer.ColorImage, 0, 1,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    createImage(mDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                framebuffer.DepthImage, framebuffer.DepthMemory);
    VkImageViewCreateInfo depthViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    depthViewInfo.image = framebuffer.DepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = mDepthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencil(mDepthFormat)) {
        depthViewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &depthViewInfo, nullptr, &framebuffer.DepthView),
            "vkCreateImageView(OOT3D Shadow2D depth)");

    const std::array<VkImageView, 2> attachments = {
        framebuffer.ColorView, framebuffer.DepthView
    };
    VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferInfo.renderPass = mOot3dShadow2dRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    CheckVk(vkCreateFramebuffer(mDevice, &framebufferInfo, nullptr,
                                &framebuffer.Framebuffer),
            "vkCreateFramebuffer(OOT3D Shadow2D)");
    framebuffer.Width = width;
    framebuffer.Height = height;
    framebuffer.ColorFormat = GfxFramebufferColorFormat::R32ui;
    framebuffer.HasDepthBuffer = true;
    return true;
}

void GfxRenderingAPIVulkan::CreateOot3dShadow2dDepthEncodePipeline() {
    if (mOot3dShadow2dDepthEncodePipeline != VK_NULL_HANDLE) {
        return;
    }
    CreateOot3dShadow2dRenderPass();
    static constexpr const char* vertexSource = R"glsl(
#version 450
layout(location = 0) in vec4 aPosition;
void main() {
    gl_Position = aPosition;
}
)glsl";
    static constexpr const char* fragmentSource = R"glsl(
#version 450
layout(location = 0) out uint outShadow;
void main() {
    uint depth24 = uint(clamp(gl_FragCoord.z, 0.0, 1.0) * 16777215.0);
    outShadow = (depth24 << 8u) | 255u;
}
)glsl";

    VkShaderModule vertexShader =
        CompileShaderModule(vertexSource, true, "oot3d_shadow2d_depth_encode.vert");
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    try {
        fragmentShader =
            CompileShaderModule(fragmentSource, false, "oot3d_shadow2d_depth_encode.frag");
    } catch (...) {
        vkDestroyShaderModule(mDevice, vertexShader, nullptr);
        throw;
    }
    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr },
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 4 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mOot3dShadow2dRenderPass;
    pipelineInfo.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(
        mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
        &mOot3dShadow2dDepthEncodePipeline);
    vkDestroyShaderModule(mDevice, vertexShader, nullptr);
    vkDestroyShaderModule(mDevice, fragmentShader, nullptr);
    CheckVk(result, "vkCreateGraphicsPipelines(OOT3D Shadow2D depth encode)");
}

GfxRenderingAPIVulkan::BufferAllocation GfxRenderingAPIVulkan::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    bool persistentlyMapped) {
    BufferAllocation buffer{};
    buffer.Size = size;
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer.Buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(mDevice, buffer.Buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &buffer.Memory),
            "vkAllocateMemory(buffer)");
    CheckVk(vkBindBufferMemory(mDevice, buffer.Buffer, buffer.Memory, 0),
            "vkBindBufferMemory");
    if (persistentlyMapped) {
        CheckVk(vkMapMemory(mDevice, buffer.Memory, 0, size, 0, &buffer.Mapped),
                "vkMapMemory(buffer)");
    }
    return buffer;
}

void GfxRenderingAPIVulkan::DestroyBuffer(BufferAllocation& buffer) {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    if (buffer.Buffer != VK_NULL_HANDLE) {
        mNriInterop.ForgetBuffer(buffer.Buffer);
    }
    if (buffer.Mapped != nullptr) {
        vkUnmapMemory(mDevice, buffer.Memory);
    }
    if (buffer.Buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(mDevice, buffer.Buffer, nullptr);
    }
    if (buffer.Memory != VK_NULL_HANDLE) {
        vkFreeMemory(mDevice, buffer.Memory, nullptr);
    }
    buffer = {};
}

VkCommandBuffer GfxRenderingAPIVulkan::BeginImmediateCommands() {
    VkCommandBufferAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    allocationInfo.commandPool = mCommandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    CheckVk(vkAllocateCommandBuffers(mDevice, &allocationInfo, &commandBuffer),
            "vkAllocateCommandBuffers(immediate)");
    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer(immediate)");
    return commandBuffer;
}

void GfxRenderingAPIVulkan::EndImmediateCommands(VkCommandBuffer commandBuffer) {
    CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(immediate)");
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    CheckVk(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE),
            "vkQueueSubmit(immediate)");
    CheckVk(vkQueueWaitIdle(mGraphicsQueue), "vkQueueWaitIdle(immediate)");
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
}

void GfxRenderingAPIVulkan::TransitionImageLayout(VkImage image, uint32_t baseMipLevel,
                                                   uint32_t levelCount,
                                                   VkImageLayout oldLayout,
                                                   VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.layerCount = 1;
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("unsupported OOT3D Vulkan image layout transition");
    }
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    EndImmediateCommands(commandBuffer);
}

void GfxRenderingAPIVulkan::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                                               uint32_t height, uint32_t mipLevel) {
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = mipLevel;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    EndImmediateCommands(commandBuffer);
}

void GfxRenderingAPIVulkan::UploadTextureLevel(TextureRecord& texture, uint32_t level,
                                                const uint8_t* rgba32Buf, uint32_t width,
                                                uint32_t height) {
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(width) * height * 4;
    auto staging = CreateBuffer(byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                true);
    std::memcpy(staging.Mapped, rgba32Buf, static_cast<size_t>(byteCount));

    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    const bool firstUpload = level == 0 && texture.UploadedMipLevels == 0;
    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = firstUpload ? VK_IMAGE_LAYOUT_UNDEFINED
                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = texture.Image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = level;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = firstUpload ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, firstUpload ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                     : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = level;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, staging.Buffer, texture.Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShaderRead{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = texture.Image;
    toShaderRead.subresourceRange = toTransfer.subresourceRange;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toShaderRead);
    EndImmediateCommands(commandBuffer);
    DestroyBuffer(staging);
    if (level == texture.UploadedMipLevels) {
        ++texture.UploadedMipLevels;
    }
}

void GfxRenderingAPIVulkan::CreateTextureImage(TextureRecord& texture,
                                                const uint8_t* rgba32Buf, uint32_t width,
                                                uint32_t height) {
    if (texture.Uploaded) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(replace texture)");
        DestroyTexture(texture);
    }
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    texture.UploadedMipLevels = 0;
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = texture.MipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &texture.Image),
            "vkCreateImage(texture)");
    texture.NriImage = texture.Image;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(mDevice, texture.Image, &requirements);
    VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex =
        FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &texture.Memory),
            "vkAllocateMemory(texture)");
    CheckVk(vkBindImageMemory(mDevice, texture.Image, texture.Memory, 0),
            "vkBindImageMemory(texture)");

    UploadTextureLevel(texture, 0, rgba32Buf, width, height);
    if (texture.MipLevels > 1) {
        TransitionImageLayout(texture.Image, 1, texture.MipLevels - 1,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = texture.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = texture.MipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &texture.View),
            "vkCreateImageView(texture)");
    texture.Uploaded = true;
    RecreateSampler(texture);
}

void GfxRenderingAPIVulkan::RecreateSampler(TextureRecord& texture) {
    if (!texture.Uploaded && texture.Image == VK_NULL_HANDLE) {
        return;
    }
    if (texture.Sampler != VK_NULL_HANDLE) {
        mRetiredSamplers.push_back(texture.Sampler);
    }
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = ToVulkanFilter(texture.SamplerState.MagFilter);
    samplerInfo.minFilter = ToVulkanFilter(texture.SamplerState.MinFilter);
    samplerInfo.mipmapMode = ToVulkanMipmapMode(texture.SamplerState.MinFilter);
    samplerInfo.addressModeU = ToVulkanAddressMode(texture.SamplerState.WrapS);
    samplerInfo.addressModeV = ToVulkanAddressMode(texture.SamplerState.WrapT);
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = texture.SamplerState.LodBias;
    const uint32_t highestUploadedMip = texture.UploadedMipLevels > 0
                                            ? texture.UploadedMipLevels - 1
                                            : 0;
    samplerInfo.minLod = static_cast<float>(
        std::min(texture.SamplerState.MinMipLevel, highestUploadedMip));
    samplerInfo.maxLod = static_cast<float>(
        std::min(texture.SamplerState.MaxMipLevel, highestUploadedMip));
    CheckVk(vkCreateSampler(mDevice, &samplerInfo, nullptr, &texture.Sampler),
            "vkCreateSampler");
    texture.AppliedSamplerState = texture.SamplerState;
    texture.SamplerStateApplied = true;
}

void GfxRenderingAPIVulkan::DestroyTexture(TextureRecord& texture) {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    if (texture.NriImage != VK_NULL_HANDLE) {
        mNriPicaTextureUploadPass.ForgetTexture(texture.NriImage);
        if (texture.NriOwnedImage) {
            mNriPicaTextureImageOwner.Destroy(texture.NriImage);
        } else {
            mNriInterop.ForgetTexture(texture.NriImage);
        }
    }
    if (texture.Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, texture.Sampler, nullptr);
    }
    if (!texture.NriOwnedImage) {
        if (texture.View != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, texture.View, nullptr);
        }
        if (!texture.ImageBorrowed && texture.Image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, texture.Image, nullptr);
        }
        if (!texture.ImageBorrowed && texture.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, texture.Memory, nullptr);
        }
    }
    if (texture.BaseImage != VK_NULL_HANDLE) {
        // The native-sized base of a derived raw-copy sample is owned here.
        mNriPicaTextureImageOwner.Destroy(texture.BaseImage);
        texture.BaseImage = VK_NULL_HANDLE;
    }
    texture.Image = VK_NULL_HANDLE;
    texture.NriImage = VK_NULL_HANDLE;
    texture.Memory = VK_NULL_HANDLE;
    texture.View = VK_NULL_HANDLE;
    texture.Sampler = VK_NULL_HANDLE;
    texture.UploadedMipLevels = 0;
    texture.SamplerStateApplied = false;
    texture.NriOwnedImage = false;
    texture.ImageBorrowed = false;
    texture.LiveRenderTargetAlias = false;
    texture.Uploaded = false;
}

VkDescriptorSet GfxRenderingAPIVulkan::AllocateTextureDescriptorSet() {
    VkDescriptorSetAllocateInfo allocationInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };
    allocationInfo.descriptorPool = mFrameResources[mCurrentFrame].DescriptorPool;
    allocationInfo.descriptorSetCount = 1;
    allocationInfo.pSetLayouts = &mTextureDescriptorSetLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    CheckVk(vkAllocateDescriptorSets(mDevice, &allocationInfo, &descriptorSet),
            "vkAllocateDescriptorSets");
    return descriptorSet;
}

GfxRenderingAPIVulkan::PipelineKey GfxRenderingAPIVulkan::BuildPipelineKey(
    const VulkanShaderProgram& shader) const {
    PipelineKey key{};
    key.ShaderId0 = shader.ShaderId0;
    key.ShaderId1 = shader.ShaderId1;
    key.VertexStride = shader.VertexStride;
    key.TextureCoordinateOffsets = shader.TextureCoordinateOffsets;
    key.ColorOffset = shader.InputOffsets[0] == VulkanShaderProgram::kAbsentAttribute
                          ? 0
                          : shader.InputOffsets[0];
    key.ColorComponents = shader.InputOffsets[0] == VulkanShaderProgram::kAbsentAttribute
                              ? 0
                              : shader.HasAlpha ? 4 : 3;
    key.UsesTexture0 = shader.UsedTextures[0];
    key.UsesVertexColor = key.ColorComponents != 0;
    key.DepthTest = mCurrentDepthTest != 0;
    key.DepthWrite = mCurrentDepthMask != 0;
    key.Decal = mCurrentZmodeDecal != 0;
    key.CullMode = mCullMode;
    key.BlendEnabled = mBlendState.Enabled;
    key.EquationRgb = mBlendState.EquationRgb;
    key.EquationAlpha = mBlendState.EquationAlpha;
    key.SourceRgb = mBlendState.SourceRgb;
    key.DestRgb = mBlendState.DestRgb;
    key.SourceAlpha = mBlendState.SourceAlpha;
    key.DestAlpha = mBlendState.DestAlpha;
    return key;
}

VkPipeline GfxRenderingAPIVulkan::GetOrCreatePipeline(const VulkanShaderProgram& shader) {
    const PipelineKey key = BuildPipelineKey(shader);
    const auto found = mPipelines.find(key);
    if (found != mPipelines.end()) {
        return found->second;
    }

    const VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_VERTEX_BIT, shader.VertexShader, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, shader.FragmentShader, "main", nullptr },
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = key.VertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.push_back({ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
    if (shader.UsedTextures[0]) {
        attributes.push_back({ 1, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[0] });
    }
    if (shader.UsedTextures[1]) {
        attributes.push_back({ 2, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[1] });
    }
    if (shader.UsesTexture2) {
        attributes.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,
                               shader.TextureCoordinateOffsets[2] });
    }
    if (shader.HasFog) {
        attributes.push_back({ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                               shader.FogOffset });
    }
    if (shader.HasGrayscale) {
        attributes.push_back({ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                               shader.GrayscaleOffset });
    }
    for (uint32_t input = 0; input < shader.NumInputs; ++input) {
        if (input >= shader.InputOffsets.size() ||
            shader.InputOffsets[input] == VulkanShaderProgram::kAbsentAttribute) {
            throw std::runtime_error("OOT3D Vulkan shader exceeds supported vertex input count");
        }
        attributes.push_back({ 6 + input, 0,
                               shader.HasAlpha ? VK_FORMAT_R32G32B32A32_SFLOAT
                                               : VK_FORMAT_R32G32B32_SFLOAT,
                               shader.InputOffsets[input] });
    }
    if (shader.ShadowCoordinateOffset != VulkanShaderProgram::kAbsentAttribute) {
        attributes.push_back({ 14, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               shader.ShadowCoordinateOffset });
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.lineWidth = 1.0f;
    rasterization.cullMode = key.CullMode == GfxNativeCullMode::KeepAll
                                 ? VK_CULL_MODE_NONE
                                 : VK_CULL_MODE_BACK_BIT;
    if (key.CullMode == GfxNativeCullMode::KeepClockwise) {
        rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    } else {
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    rasterization.depthBiasEnable = key.Decal ? VK_TRUE : VK_FALSE;
    rasterization.depthBiasConstantFactor = key.Decal ? -2.0f : 0.0f;
    rasterization.depthBiasSlopeFactor = key.Decal ? -2.0f : 0.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencil.depthTestEnable = key.DepthTest || key.DepthWrite;
    depthStencil.depthWriteEnable = key.DepthWrite;
    depthStencil.depthCompareOp = key.DepthTest
                                      ? key.Decal ? VK_COMPARE_OP_LESS_OR_EQUAL
                                                  : VK_COMPARE_OP_LESS
                                      : VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = key.BlendEnabled;
    blendAttachment.colorBlendOp = ToVulkanBlendOperation(key.EquationRgb);
    blendAttachment.alphaBlendOp = ToVulkanBlendOperation(key.EquationAlpha);
    blendAttachment.srcColorBlendFactor = ToVulkanBlendFactor(key.SourceRgb);
    blendAttachment.dstColorBlendFactor = ToVulkanBlendFactor(key.DestRgb);
    blendAttachment.srcAlphaBlendFactor = ToVulkanBlendFactor(key.SourceAlpha);
    blendAttachment.dstAlphaBlendFactor = ToVulkanBlendFactor(key.DestAlpha);
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR,
                                             VK_DYNAMIC_STATE_BLEND_CONSTANTS };
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    CheckVk(vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
                                      &pipeline),
            "vkCreateGraphicsPipelines");
    mPipelines.emplace(key, pipeline);
    return pipeline;
}

void GfxRenderingAPIVulkan::DestroyPresentationPipelines() {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    for (const auto& [key, pipeline] : mPipelines) {
        vkDestroyPipeline(mDevice, pipeline, nullptr);
    }
    mPipelines.clear();
    if (mNativePicaScanoutPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mNativePicaScanoutPipeline, nullptr);
        mNativePicaScanoutPipeline = VK_NULL_HANDLE;
    }
    if (mNativePicaScanoutOverlayPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mNativePicaScanoutOverlayPipeline, nullptr);
        mNativePicaScanoutOverlayPipeline = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::DestroyGraphicsPipelines() {
    DestroyPresentationPipelines();
    if (mDevice == VK_NULL_HANDLE) return;
    DestroyNativePicaPipelines();
    if (mOot3dShadow2dDepthEncodePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mDevice, mOot3dShadow2dDepthEncodePipeline, nullptr);
        mOot3dShadow2dDepthEncodePipeline = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::DestroyNativePicaPipelines() {
    for (const auto& [key, record] : mNativePicaPipelines) {
        mNriPicaPipelineBridge.ForgetOwned(record.Id);
        if (record.Vulkan != VK_NULL_HANDLE) {
            mNriPicaPipelineBridge.Forget(record.Vulkan);
            vkDestroyPipeline(mDevice, record.Vulkan, nullptr);
        }
    }
    mNativePicaPipelines.clear();
    mPicaPipelinePrewarmedProfiles.clear();
}

} // namespace Fast

#endif
