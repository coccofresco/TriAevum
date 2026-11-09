// Native PICA on Vulkan: shader and geometry resources, render passes, render targets, display images,
// memory fills and descriptor sets. Other topics: gfx_vulkan_pica_*.cpp.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/extension/native_ui_overlay.h"

namespace Fast {

void GfxRenderingAPIVulkan::CreateNativePicaShaderResources() {
    CreateNativePicaRenderPass();
    VkPhysicalDeviceFeatures seamFeatures{};
    vkGetPhysicalDeviceFeatures(mPhysicalDevice, &seamFeatures);
    mPbrSeamsAvailable = seamFeatures.geometryShader == VK_TRUE;
    std::array<VkDescriptorSetLayoutBinding, 14 + Renderer3ds::kPicaPbrTextureCount> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    for (uint32_t binding = 1; binding <= 3; ++binding) {
        bindings[binding] = {binding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[7] = {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[8] = {12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[9] = {13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                  VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[10] = {15, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[11] = {16, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[12] = {17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t index = 13U; index < 13U + Renderer3ds::kPicaPbrTextureCount; ++index)
        bindings[index] = {index + 5U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings.back() = {26, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorInfo.pBindings = bindings.data();
    CheckNativeVk(vkCreateDescriptorSetLayout(
                      mDevice, &descriptorInfo, nullptr,
                      &mNativePicaDescriptorSetLayout),
                  "vkCreateDescriptorSetLayout(native PICA)");

    VkPipelineLayoutCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &mNativePicaDescriptorSetLayout;
    VkPushConstantRange nativeDrawPush{};
    nativeDrawPush.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    nativeDrawPush.size = 8U * sizeof(float);
    pipelineInfo.pushConstantRangeCount = 1;
    pipelineInfo.pPushConstantRanges = &nativeDrawPush;
    CheckNativeVk(vkCreatePipelineLayout(mDevice, &pipelineInfo, nullptr,
                                         &mNativePicaPipelineLayout),
                  "vkCreatePipelineLayout(native PICA)");

    std::array<VkDescriptorSetLayoutBinding, 11> scanoutBindings{};
    for (uint32_t binding = 0; binding < scanoutBindings.size(); ++binding) {
        scanoutBindings[binding].binding = binding;
        scanoutBindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scanoutBindings[binding].descriptorCount = 1;
        scanoutBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    descriptorInfo.bindingCount = static_cast<uint32_t>(scanoutBindings.size());
    descriptorInfo.pBindings = scanoutBindings.data();
    CheckNativeVk(vkCreateDescriptorSetLayout(
                      mDevice, &descriptorInfo, nullptr,
                      &mNativePicaScanoutDescriptorSetLayout),
                  "vkCreateDescriptorSetLayout(native PICA scanout)");

    VkPushConstantRange scanoutPushConstant{};
    scanoutPushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    scanoutPushConstant.size = sizeof(Extension::PicaScanoutPushConstants);
    pipelineInfo.pSetLayouts = &mNativePicaScanoutDescriptorSetLayout;
    pipelineInfo.pushConstantRangeCount = 1;
    pipelineInfo.pPushConstantRanges = &scanoutPushConstant;
    CheckNativeVk(vkCreatePipelineLayout(
                      mDevice, &pipelineInfo, nullptr,
                      &mNativePicaScanoutPipelineLayout),
                  "vkCreatePipelineLayout(native PICA scanout)");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    CheckNativeVk(vkCreateSampler(mDevice, &samplerInfo, nullptr,
                                  &mNativePicaScanoutSampler),
                  "vkCreateSampler(native PICA scanout)");
}

void GfxRenderingAPIVulkan::DestroyNativePicaShaderResources() {
    EndNativePicaRenderPass();
    for (auto& [key, texture] : mNativePicaTextures) {
        DestroyTexture(texture);
    }
    mNativePicaTextures.clear();
    for (auto& [hash, material] : mNativePicaMaterials) {
        for (auto& image : material.Active.Images) DestroyTexture(image);
        for (auto& image : material.Pending.Images) DestroyTexture(image);
    }
    mNativePicaMaterials.clear();
    for (auto& frame : mFrameResources) {
        for (auto& image : frame.RetiredTextures) DestroyTexture(image);
        frame.RetiredTextures.clear();
    }
    for (auto& [key, lightingLut] : mNativePicaLightingLuts) {
        DestroyTexture(lightingLut.Texture);
    }
    mNativePicaLightingLuts.clear();
    for (auto& [key, target] : mNativePicaRenderTargets) {
        DestroyNativePicaRenderTarget(target);
    }
    mNativePicaRenderTargets.clear();
    mNativePicaDisplayDepthTargets.clear();
    for (auto& [address, image] : mNativePicaDisplayImages) {
        DestroyNativePicaDisplayImage(image);
    }
    mNativePicaDisplayImages.clear();
    for (uint32_t slot = 0; slot < kFramesInFlight; ++slot) {
        ReleaseRetiredNativePicaDisplayImages(slot);
    }
    DestroyNativePicaSceneBeforeUiImages();
    const auto destroyShaderCache = [this](auto& cache) {
        for (auto& [key, shader] : cache) {
            (void)key;
            if (shader.VertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    mDevice, shader.VertexShader, nullptr);
            }
            if (shader.FragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    mDevice, shader.FragmentShader, nullptr);
            }
        }
        cache.clear();
    };
    destroyShaderCache(mCanonicalNativePicaShaders);
    destroyShaderCache(mInstrumentedNativePicaShaders);
    destroyShaderCache(mUberNativePicaShaders);
    if (mNativePicaScanoutSampler != VK_NULL_HANDLE) {
        vkDestroySampler(mDevice, mNativePicaScanoutSampler, nullptr);
        mNativePicaScanoutSampler = VK_NULL_HANDLE;
    }
    if (mNativePicaScanoutPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(mDevice, mNativePicaScanoutPipelineLayout,
                                nullptr);
        mNativePicaScanoutPipelineLayout = VK_NULL_HANDLE;
    }
    if (mNativePicaScanoutDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            mDevice, mNativePicaScanoutDescriptorSetLayout, nullptr);
        mNativePicaScanoutDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (mNativePicaPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(mDevice, mNativePicaPipelineLayout, nullptr);
        mNativePicaPipelineLayout = VK_NULL_HANDLE;
    }
    if (mNativePicaDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(mDevice, mNativePicaDescriptorSetLayout,
                                     nullptr);
        mNativePicaDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (mNativePicaRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mNativePicaRenderPass, nullptr);
        mNativePicaRenderPass = VK_NULL_HANDLE;
    }
    if (mNativePicaCanonicalRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mNativePicaCanonicalRenderPass, nullptr);
        mNativePicaCanonicalRenderPass = VK_NULL_HANDLE;
    }
}

void GfxRenderingAPIVulkan::RetireNativePicaGeometry(uint64_t identity) {
    const auto found = mNativePicaGeometryBuffers.find(identity);
    if (found == mNativePicaGeometryBuffers.end()) {
        return;
    }
    for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; ++frameIndex) {
        auto& buffer = found->second.Buffers[frameIndex];
        if (buffer.Buffer == VK_NULL_HANDLE) {
            continue;
        }
        mRetiredNativePicaGeometryBuffers[frameIndex].push_back(buffer);
        buffer = {};
    }
    mNativePicaGeometryBuffers.erase(found);
}

void GfxRenderingAPIVulkan::ReleaseRetiredNativePicaGeometryBuffers(uint32_t frameIndex) {
    auto& retired = mRetiredNativePicaGeometryBuffers[frameIndex % kFramesInFlight];
    for (auto& buffer : retired) {
        if (buffer.Buffer != VK_NULL_HANDLE) {
            mNriInterop.ForgetBuffer(buffer.Buffer);
        }
        DestroyBuffer(buffer);
    }
    retired.clear();
}

void GfxRenderingAPIVulkan::DestroyNativePicaGeometryResources() {
    for (auto& [_, geometry] : mNativePicaGeometryBuffers) {
        for (auto& buffer : geometry.Buffers) {
            if (buffer.Buffer != VK_NULL_HANDLE) {
                mNriInterop.ForgetBuffer(buffer.Buffer);
            }
            DestroyBuffer(buffer);
        }
    }
    mNativePicaGeometryBuffers.clear();
    for (auto& retired : mRetiredNativePicaGeometryBuffers) {
        for (auto& buffer : retired) {
            if (buffer.Buffer != VK_NULL_HANDLE) {
                mNriInterop.ForgetBuffer(buffer.Buffer);
            }
            DestroyBuffer(buffer);
        }
        retired.clear();
    }
    mPicaGeometryRegistry.Clear();
    mPicaSceneFrame.Reset();
    mNativeSceneView.Reset();
    mPreparedShadowCasterFrame.Reset();
    mPreparedShadowCasterView.Reset();
    mPreparedShadowCasters.clear();
    mPicaScenePublications.Reset();
    mGrassGeometryRegistry.Clear();
    mGrassDrawMemo.Clear();
    Extension::GrassSceneBridge::Instance().Clear();
}

void GfxRenderingAPIVulkan::CreateNativePicaScanoutPipeline() {
    if (mNativePicaScanoutPipeline != VK_NULL_HANDLE &&
        mNativePicaScanoutOverlayPipeline != VK_NULL_HANDLE) {
        return;
    }
    if (mRenderPass == VK_NULL_HANDLE || mOverlayRenderPass == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "native PICA scanout requires swapchain base and overlay render passes");
    }

    const std::string vertexSource =
        Extension::BuildPicaScanoutVertexShader();
    const std::string fragmentSource =
        Extension::BuildPicaScanoutFragmentShader();

    VkShaderModule vertexShader = CreateShaderModuleFromSpirv(ResolveNativePicaShaderSpirv(
        vertexSource, Extension::PicaAotShaderStage::Vertex, true, "oot3d_native_pica_scanout.vert"));
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    try {
        fragmentShader = CreateShaderModuleFromSpirv(ResolveNativePicaShaderSpirv(
            fragmentSource, Extension::PicaAotShaderStage::Fragment, false, "oot3d_native_pica_scanout.frag"));
    } catch (...) {
        vkDestroyShaderModule(mDevice, vertexShader, nullptr);
        throw;
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mNativePicaScanoutPipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    VkResult result = VK_SUCCESS;
    if (mNativePicaScanoutPipeline == VK_NULL_HANDLE) {
        result = vkCreateGraphicsPipelines(
            mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
            &mNativePicaScanoutPipeline);
    }
    if (result == VK_SUCCESS &&
        mNativePicaScanoutOverlayPipeline == VK_NULL_HANDLE) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        pipelineInfo.renderPass = mOverlayRenderPass;
        result = vkCreateGraphicsPipelines(
            mDevice, mPipelineCache, 1, &pipelineInfo, nullptr,
            &mNativePicaScanoutOverlayPipeline);
    }
    vkDestroyShaderModule(mDevice, vertexShader, nullptr);
    vkDestroyShaderModule(mDevice, fragmentShader, nullptr);
    CheckVk(result, "vkCreateGraphicsPipelines(native PICA scanout)");
}

void GfxRenderingAPIVulkan::CreateNativePicaRenderPass() {
    if (mNativePicaRenderPass != VK_NULL_HANDLE && mNativePicaCanonicalRenderPass != VK_NULL_HANDLE) {
        return;
    }
    if (mDepthFormat == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("native PICA render pass requires a resolved depth format");
    }
    // Android's API-29 loader stub does not export Vulkan 1.2 entry points.
    // Resolve against the selected device, whose Vulkan capabilities are checked
    // at initialization; this also works with desktop and custom driver loaders.
    const auto createRenderPass2 = reinterpret_cast<PFN_vkCreateRenderPass2>(
        vkGetDeviceProcAddr(mDevice, "vkCreateRenderPass2"));
    if (createRenderPass2 == nullptr) {
        throw std::runtime_error("native PICA requires Vulkan vkCreateRenderPass2");
    }
    const auto createRenderPass = [this, createRenderPass2](uint32_t colorCount, VkRenderPass& renderPass) {
        const bool multisampled = mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT;
        const uint32_t depthIndex = colorCount;
        const uint32_t resolveBase = colorCount + 1U;
        const uint32_t depthResolveIndex = resolveBase + colorCount;
        std::vector<VkAttachmentDescription2> attachments(multisampled ? depthResolveIndex + 1U : depthIndex + 1U);
        for (auto& attachment : attachments) {
            attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        }
        constexpr std::array<VkFormat, Extension::kPicaColorAttachmentCount> colorFormats{
            VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R32G32B32A32_UINT
        };
        for (uint32_t index = 0; index < colorCount; ++index) {
            attachments[index].format = colorFormats[index];
            attachments[index].samples = mNativePicaSampleCount;
            attachments[index].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[index].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[depthIndex].format = mDepthFormat;
        attachments[depthIndex].samples = mNativePicaSampleCount;
        attachments[depthIndex].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[depthIndex].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[depthIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[depthIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[depthIndex].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[depthIndex].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        std::array<VkAttachmentReference2, Extension::kPicaColorAttachmentCount> colorReferences{};
        for (uint32_t index = 0; index < colorCount; ++index) {
            colorReferences[index] = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, index,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT };
        }
        VkAttachmentReference2 depthReference{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, depthIndex,
                                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                               NativeDepthAspect(mDepthFormat) };
        std::array<VkAttachmentReference2, Extension::kPicaColorAttachmentCount> resolveReferences{};
        VkAttachmentReference2 depthResolveReference{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr,
                                                      depthResolveIndex,
                                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                      VK_IMAGE_ASPECT_DEPTH_BIT };
        VkSubpassDescriptionDepthStencilResolve depthResolve{
            VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE
        };
        if (multisampled) {
            for (uint32_t index = 0; index < colorCount; ++index) {
                attachments[resolveBase + index] = attachments[index];
                attachments[resolveBase + index].samples = VK_SAMPLE_COUNT_1_BIT;
                resolveReferences[index] = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, resolveBase + index,
                                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT };
            }
            attachments[depthResolveIndex] = attachments[depthIndex];
            attachments[depthResolveIndex].samples = VK_SAMPLE_COUNT_1_BIT;
            depthResolve.depthResolveMode = mNativePicaDepthResolveMode;
            depthResolve.stencilResolveMode = VK_RESOLVE_MODE_NONE;
            depthResolve.pDepthStencilResolveAttachment = &depthResolveReference;
        }
        VkSubpassDescription2 subpass{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2 };
        subpass.pNext = multisampled ? &depthResolve : nullptr;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = colorCount;
        subpass.pColorAttachments = colorReferences.data();
        subpass.pResolveAttachments = multisampled ? resolveReferences.data() : nullptr;
        subpass.pDepthStencilAttachment = &depthReference;
        std::array<VkSubpassDependency2, 2> dependencies{};
        for (auto& dependency : dependencies) {
            dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        }
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkRenderPassCreateInfo2 info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 };
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        CheckNativeVk(createRenderPass2(mDevice, &info, nullptr, &renderPass), "vkCreateRenderPass2(native PICA)");
    };
    createRenderPass(1U, mNativePicaCanonicalRenderPass);
    // The legacy render pass has no albedo guide; GI requires dynamic rendering.
    createRenderPass(static_cast<uint32_t>(Extension::kPicaBaseInstrumentedColorAttachmentCount), mNativePicaRenderPass);
}

void GfxRenderingAPIVulkan::ApplyNativePicaSampleCount(VkSampleCountFlagBits sampleCount) {
    if (sampleCount == mNativePicaSampleCount) return;
    if (sampleCount != VK_SAMPLE_COUNT_1_BIT &&
        ((mNativePicaSupportedSampleCounts & sampleCount) == 0U ||
         mNativePicaDepthResolveMode == VK_RESOLVE_MODE_NONE)) {
        sampleCount = VK_SAMPLE_COUNT_1_BIT;
    }
    WaitForAllPresents();
    CheckNativeVk(vkDeviceWaitIdle(mDevice),
                  "vkDeviceWaitIdle(native PICA MSAA transaction)");
    EndNativePicaRenderPass();
    ForgetNativePicaEffectNriTextures();
    mAmbientOcclusionPass.InvalidateScreenResources();
    mCacaoPass.InvalidateScreenResources();
    mMxaoPass.InvalidateScreenResources();
    mFidelityFxSssrPass.InvalidateScreenResources();
    mBrixelizerGiPass.InvalidateScreenResources();
    mScreenSpaceGi.InvalidateScreenResources();
    mReflectionMaterialResolvePass.InvalidateScreenResources();
    mLinearSceneColorPass.InvalidateScreenResources();
    mHiZDepthPyramidPass.InvalidateScreenResources();
    if (mHiZReflectionSurface.has_value()) {
        mSceneSurfaces.Retire(*mHiZReflectionSurface);
        mHiZReflectionSurface.reset();
    }
    mHiZReflectionPass.InvalidateScreenResources();
    if (mMotionSurface.has_value()) {
        mSceneSurfaces.Retire(*mMotionSurface);
        mMotionSurface.reset();
    }
    mMotionVectorPass.InvalidateScreenResources();
    if (mLinearColorSurface.has_value()) {
        mSceneSurfaces.Retire(*mLinearColorSurface);
        mLinearColorSurface.reset();
    }
    mNriUpscalerPass.InvalidateScreenResources();
    mUpscalerGuidePass.InvalidateScreenResources();
    mNativeUiOverlayPass.InvalidateScreenResources();
    if (mCompositeSurface.has_value()) {
        mSceneSurfaces.Retire(*mCompositeSurface);
        mCompositeSurface.reset();
    }
    mSceneCompositePass.InvalidateScreenResources();
    mSmaa1xPass.InvalidateScreenResources();
    mNriEffectGraphTransientImageArena.InvalidateScreenResources();
    if (mTaaSurface.has_value()) {
        mSceneSurfaces.Retire(*mTaaSurface);
        mTaaSurface.reset();
    }
    mTemporalAaPass.InvalidateScreenResources();
    mTaaOutputValid = false;
    mAmbientOcclusionOutputValid = false;
    mHiZOutputValid = false;
    mInteractiveGrassPass.Shutdown();
    mWorldDebugLinePass.Shutdown();
    mTemporalHistory.Reset();
    mPicaTemporalHistory.Reset();
    for (auto& [key, target] : mNativePicaRenderTargets) {
        DestroyNativePicaRenderTarget(target);
    }
    mNativePicaRenderTargets.clear();
    mNativePicaDisplayDepthTargets.clear();
    DestroyNativePicaUiTargets();
    DestroyNativePicaPipelines();
    if (mNativePicaRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mNativePicaRenderPass, nullptr);
        mNativePicaRenderPass = VK_NULL_HANDLE;
    }
    if (mNativePicaCanonicalRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mNativePicaCanonicalRenderPass, nullptr);
        mNativePicaCanonicalRenderPass = VK_NULL_HANDLE;
    }
    mActiveNativePicaRenderTarget = nullptr;
    mNativePicaRenderPassActive = false;
    mNativePicaSampleCount = sampleCount;
    CreateNativePicaRenderPass();
    if (!mInteractiveGrassPass.Initialize(mPhysicalDevice, mDevice,
                                           mNriInterop.Shaders(),
                                           mNativePicaCanonicalRenderPass,
                                           mNativePicaRenderPass,
                                           mNativePicaSampleCount,
                                           mPicaDynamicRenderingScope.Available(),
                                           mDepthFormat, mPipelineCache)) {
        SPDLOG_INFO("OOT3D Vulkan interactive grass unavailable after MSAA change: {}",
                    mInteractiveGrassPass.UnavailableReason());
    }
    mWorldDebugLinePass.Initialize(mPhysicalDevice, mDevice, mNriInterop.Shaders(),
                                   mNativePicaCanonicalRenderPass, mNativePicaRenderPass,
                                   mNativePicaSampleCount, mPicaDynamicRenderingScope.Available(),
                                   mDepthFormat, mPipelineCache);
    SPDLOG_INFO("OOT3D Vulkan native PICA MSAA changed to {} samples",
                static_cast<uint32_t>(mNativePicaSampleCount));
}

std::optional<Extension::PerspectiveViewState> GfxRenderingAPIVulkan::SamplePerspective(
    const NativePicaRenderTarget* target) const {
    auto perspective = Extension::SceneViewRuntime::Instance().LatestPerspective();
    if (!perspective.has_value() || target == nullptr) return perspective;
    // The rendered depth follows the projection the draws applied, which the
    // game adjusts after building it from the camera's frustum.
    if (const auto projection = target->SampleProjection.Resolve(mFrameCounter))
        perspective = Extension::ApplySampleProjection(*perspective, *projection);
    if (const auto view = target->SampleView.Resolve(mFrameCounter))
        return Extension::ResolveSamplePerspective(*perspective, *view);
    return perspective;
}

GfxRenderingAPIVulkan::NativePicaRenderTarget&
GfxRenderingAPIVulkan::GetOrCreateNativePicaRenderTarget(
    const GfxNativePicaDrawView& draw,
    uint32_t restoredWidth, uint32_t restoredHeight, bool outputResolutionUi) {
    auto& targets = outputResolutionUi ? mNativePicaUiTargets : mNativePicaRenderTargets;
    NativePicaRenderTargetKey key{
        draw.RenderTargetNamespace,
        draw.FramebufferColorPhysicalAddress,
        draw.FramebufferDepthPhysicalAddress,
        draw.FramebufferWidth,
        draw.FramebufferHeight,
        draw.FramebufferColorFormat,
        draw.FramebufferDepthFormat,
        static_cast<uint16_t>(std::lround(mInternalResolutionScale * 1000.0F))};
    const bool restoredExtentAvailable =
        restoredWidth != 0U && restoredHeight != 0U;
    // Targets follow the presentation picture (the aspect ratio's letterbox
    // inside the swapchain), the output the scene is shown in.
    const auto outputExtent = NativePicaOutputExtent();
    const auto targetExtent = restoredExtentAvailable
        ? Extension::RenderExtent{restoredWidth, restoredHeight}
        : Extension::ResolveNativePicaRenderExtent(
              {key.Width, key.Height},
              NativeTopScreenOutputExtent(),
              outputResolutionUi ? 1.0F : mInternalResolutionScale);
    if (const auto found = targets.find(key);
        found != targets.end()) {
        mDiagnostics.RecordRenderResolution(mInternalResolutionScale, outputExtent.Width,
            outputExtent.Height, found->second.Width, found->second.Height);
        PrepareNativePicaTargetAttachments(found->second);
        return found->second;
    }
    mDiagnostics.RecordRenderResolution(mInternalResolutionScale, outputExtent.Width,
        outputExtent.Height, targetExtent.Width, targetExtent.Height);

    NativePicaRenderTarget target;
    target.Key = key;
    target.OutputResolutionUi = outputResolutionUi;
    target.Width = targetExtent.Width;
    target.Height = targetExtent.Height;
    // A target allocates every base guide together; a frame binds the ones
    // its effects require (ActivePicaAttachmentRequirements).
    target.Attachments = ActivePicaAttachmentRequirements().BaseGuides();
    if (!target.Attachments.NativeColorOnly())
        target.Attachments.AuxiliaryOutputs |= Extension::kAllPicaAuxiliaryOutputs;
    const bool instrumented = !target.Attachments.NativeColorOnly();
    VkFormatProperties shadowFormatProperties{};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, VK_FORMAT_R32_UINT,
                                        &shadowFormatProperties);
    if ((shadowFormatProperties.optimalTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT) == 0U) {
        throw std::runtime_error(
            "VK_FORMAT_R32_UINT storage image atomics are unavailable");
    }

    Extension::NriPicaRenderTargetImages nriImages;
    if (mNriPicaRenderTargetOwner.Create(
            {target.Width, target.Height, mDepthFormat,
             mNativePicaSampleCount, target.Attachments},
            nriImages)) {
        target.ColorImage = nriImages.Color;
        target.NormalGuideImage = nriImages.NormalGuide;
        target.MaterialGuideImage = nriImages.MaterialGuide;
        target.RigidMotionGuideImage = nriImages.RigidMotionGuide;
        target.AmbientGuideImage = nriImages.AmbientGuide;
        target.FogGuideImage = nriImages.FogGuide;
        target.OutlineGeometryGuideImage = nriImages.OutlineGeometryGuide;
        target.ShadowImage = nriImages.Shadow;
        target.DepthImage = nriImages.Depth;
        target.MsaaColorImage = nriImages.MsaaColor;
        target.MsaaNormalGuideImage = nriImages.MsaaNormalGuide;
        target.MsaaMaterialGuideImage = nriImages.MsaaMaterialGuide;
        target.MsaaRigidMotionGuideImage =
            nriImages.MsaaRigidMotionGuide;
        target.MsaaAmbientGuideImage = nriImages.MsaaAmbientGuide;
        target.MsaaFogGuideImage = nriImages.MsaaFogGuide;
        target.MsaaOutlineGeometryGuideImage = nriImages.MsaaOutlineGeometryGuide;
        target.MsaaDepthImage = nriImages.MsaaDepth;
        target.NriOwned = true;
        mDiagnostics.RecordNriPicaRenderTarget(
            true, Extension::CountNriPicaRenderTargetImages(nriImages));
    } else {
        const auto createImage = [&](
                                     VkFormat format,
                                     VkImageUsageFlags usage,
                                     VkSampleCountFlagBits samples,
                                     VkImage& image,
                                     VkDeviceMemory& memory) {
            VkImageCreateInfo imageInfo{
                VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {target.Width, target.Height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = samples;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            CheckNativeVk(
                vkCreateImage(mDevice, &imageInfo, nullptr, &image),
                "vkCreateImage(native PICA target)");
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(mDevice, image, &requirements);
            VkMemoryAllocateInfo allocation{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = FindMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            CheckNativeVk(
                vkAllocateMemory(
                    mDevice, &allocation, nullptr, &memory),
                "vkAllocateMemory(native PICA target)");
            CheckNativeVk(
                vkBindImageMemory(mDevice, image, memory, 0),
                "vkBindImageMemory(native PICA target)");
        };
        createImage(
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT, target.ColorImage,
            target.ColorMemory);
        if (instrumented) {
            createImage(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT, target.NormalGuideImage,
                target.NormalGuideMemory);
            createImage(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT, target.MaterialGuideImage,
                target.MaterialGuideMemory);
            createImage(
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT, target.RigidMotionGuideImage,
                target.RigidMotionGuideMemory);
            createImage(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT, target.AmbientGuideImage,
                target.AmbientGuideMemory);
            createImage(VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT, target.FogGuideImage, target.FogGuideMemory);
            createImage(VK_FORMAT_R32G32B32A32_UINT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_SAMPLE_COUNT_1_BIT, target.OutlineGeometryGuideImage, target.OutlineGeometryGuideMemory);
        }
        createImage(
            VK_FORMAT_R32_UINT,
            VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT, target.ShadowImage,
            target.ShadowMemory);
        createImage(
            mDepthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT, target.DepthImage,
            target.DepthMemory);
        if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            const VkImageUsageFlags msaaColorUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            createImage(
                VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage,
                mNativePicaSampleCount, target.MsaaColorImage,
                target.MsaaColorMemory);
            if (instrumented) {
                createImage(
                    VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage,
                    mNativePicaSampleCount, target.MsaaNormalGuideImage,
                    target.MsaaNormalGuideMemory);
                createImage(
                    VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage,
                    mNativePicaSampleCount, target.MsaaMaterialGuideImage,
                    target.MsaaMaterialGuideMemory);
                createImage(
                    VK_FORMAT_R16G16B16A16_SFLOAT, msaaColorUsage,
                    mNativePicaSampleCount,
                    target.MsaaRigidMotionGuideImage,
                    target.MsaaRigidMotionGuideMemory);
                createImage(
                    VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage,
                    mNativePicaSampleCount,
                    target.MsaaAmbientGuideImage,
                    target.MsaaAmbientGuideMemory);
                createImage(VK_FORMAT_R8G8B8A8_UNORM, msaaColorUsage,
                    mNativePicaSampleCount, target.MsaaFogGuideImage, target.MsaaFogGuideMemory);
                createImage(VK_FORMAT_R32G32B32A32_UINT, msaaColorUsage, mNativePicaSampleCount,
                            target.MsaaOutlineGeometryGuideImage, target.MsaaOutlineGeometryGuideMemory);
            }
            createImage(
                mDepthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                mNativePicaSampleCount, target.MsaaDepthImage,
                target.MsaaDepthMemory);
        }
    }

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    view.image = target.ColorImage;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                    &target.ColorView),
                  "vkCreateImageView(native PICA color)");
    if (instrumented) {
        view.image = target.NormalGuideImage;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.NormalGuideView),
                      "vkCreateImageView(native PICA normal guide)");
        view.image = target.MaterialGuideImage;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.MaterialGuideView),
                      "vkCreateImageView(native PICA material guide)");
        view.image = target.RigidMotionGuideImage;
        view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.RigidMotionGuideView),
                      "vkCreateImageView(native PICA rigid motion guide)");
        view.image = target.AmbientGuideImage;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.AmbientGuideView),
                      "vkCreateImageView(native PICA ambient guide)");
        view.image = target.FogGuideImage;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr, &target.FogGuideView),
                      "vkCreateImageView(native PICA fog guide)");
        view.image = target.OutlineGeometryGuideImage;
        view.format = VK_FORMAT_R32G32B32A32_UINT;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr, &target.OutlineGeometryGuideView),
                      "vkCreateImageView(native PICA outline geometry guide)");
    }
    view.image = target.ShadowImage;
    view.format = VK_FORMAT_R32_UINT;
    CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                    &target.ShadowView),
                  "vkCreateImageView(native PICA shadow storage)");
    view.image = target.DepthImage;
    view.format = mDepthFormat;
    view.subresourceRange.aspectMask = NativeDepthAspect(mDepthFormat);
    CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                    &target.DepthView),
                  "vkCreateImageView(native PICA depth)");
    if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.image = target.MsaaColorImage;
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.MsaaColorView),
                      "vkCreateImageView(native PICA MSAA color)");
        if (instrumented) {
            view.image = target.MsaaNormalGuideImage;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                            &target.MsaaNormalGuideView),
                          "vkCreateImageView(native PICA MSAA normal guide)");
            view.image = target.MsaaMaterialGuideImage;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                            &target.MsaaMaterialGuideView),
                          "vkCreateImageView(native PICA MSAA material guide)");
            view.image = target.MsaaRigidMotionGuideImage;
            view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                            &target.MsaaRigidMotionGuideView),
                          "vkCreateImageView(native PICA MSAA rigid motion guide)");
            view.image = target.MsaaAmbientGuideImage;
            view.format = VK_FORMAT_R8G8B8A8_UNORM;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                            &target.MsaaAmbientGuideView),
                          "vkCreateImageView(native PICA MSAA ambient guide)");
            view.image = target.MsaaFogGuideImage;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr, &target.MsaaFogGuideView),
                          "vkCreateImageView(native PICA MSAA fog guide)");
            view.image = target.MsaaOutlineGeometryGuideImage;
            view.format = VK_FORMAT_R32G32B32A32_UINT;
            CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr, &target.MsaaOutlineGeometryGuideView),
                          "vkCreateImageView(native PICA MSAA outline geometry guide)");
        }
        view.image = target.MsaaDepthImage;
        view.format = mDepthFormat;
        view.subresourceRange.aspectMask = NativeDepthAspect(mDepthFormat);
        CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                        &target.MsaaDepthView),
                      "vkCreateImageView(native PICA MSAA depth)");
    }

    std::vector<VkImageView> views;
    if (mNativePicaSampleCount == VK_SAMPLE_COUNT_1_BIT && instrumented) {
        views = { target.ColorView,
                  target.NormalGuideView,
                  target.MaterialGuideView,
                  target.RigidMotionGuideView,
                  target.AmbientGuideView,
                  target.FogGuideView,
                  target.OutlineGeometryGuideView,
                  target.DepthView };
    } else if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT &&
               instrumented) {
        views = { target.MsaaColorView,
                  target.MsaaNormalGuideView,
                  target.MsaaMaterialGuideView,
                  target.MsaaRigidMotionGuideView,
                  target.MsaaAmbientGuideView,
                  target.MsaaFogGuideView,
                  target.MsaaOutlineGeometryGuideView,
                  target.MsaaDepthView,
                  target.ColorView,
                  target.NormalGuideView,
                  target.MaterialGuideView,
                  target.RigidMotionGuideView,
                  target.AmbientGuideView,
                  target.FogGuideView,
                  target.OutlineGeometryGuideView,
                  target.DepthView };
    } else if (mNativePicaSampleCount == VK_SAMPLE_COUNT_1_BIT) {
        views = {target.ColorView, target.DepthView};
    } else {
        views = {target.MsaaColorView, target.MsaaDepthView,
                 target.ColorView, target.DepthView};
    }
    VkFramebufferCreateInfo framebuffer{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = instrumented
        ? mNativePicaRenderPass
        : mNativePicaCanonicalRenderPass;
    framebuffer.attachmentCount = static_cast<uint32_t>(views.size());
    framebuffer.pAttachments = views.data();
    framebuffer.width = target.Width;
    framebuffer.height = target.Height;
    framebuffer.layers = 1;
    CheckNativeVk(vkCreateFramebuffer(mDevice, &framebuffer, nullptr, &target.Framebuffer),
                  "vkCreateFramebuffer(native PICA)");
    if (!instrumented) {
        target.CanonicalFramebuffer = target.Framebuffer;
        target.Framebuffer = VK_NULL_HANDLE;
    }

    if (mNriPicaRenderTargetInitPass.Available()) {
        EndNativePicaRenderPass();
        if (mRenderPassActive) {
            vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
            mRenderPassActive = false;
            mOverlayRenderPassActive = false;
        }
    }
    const Extension::NriPicaRenderTargetImages initImages{
        target.ColorImage,
        target.NormalGuideImage,
        target.MaterialGuideImage,
        target.RigidMotionGuideImage,
        target.AmbientGuideImage,
        target.FogGuideImage,
        target.OutlineGeometryGuideImage,
        target.ShadowImage,
        target.DepthImage,
        target.MsaaColorImage,
        target.MsaaNormalGuideImage,
        target.MsaaMaterialGuideImage,
        target.MsaaRigidMotionGuideImage,
        target.MsaaAmbientGuideImage,
        target.MsaaFogGuideImage,
        target.MsaaOutlineGeometryGuideImage,
        target.MsaaDepthImage,
    };
    const bool nriInitialized =
        mFrameActive && mNriPicaRenderTargetInitPass.Execute({
            mCurrentFrame,
            target.Width,
            target.Height,
            mDepthFormat,
            mNativePicaSampleCount,
            target.Attachments,
            initImages,
        });
    if (nriInitialized) {
        mDiagnostics.RecordNriPicaRenderTargetInitialization(true, mNriPicaRenderTargetInitPass.LastClearedImageCount(),
                                                             mNriPicaRenderTargetInitPass.LastBarrierCount());
    } else if (instrumented) {
        VkCommandBuffer command = BeginImmediateCommands();
        std::array<VkImageMemoryBarrier, 9> toTransfer{};
        toTransfer[0] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        toTransfer[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer[0].image = target.ColorImage;
        toTransfer[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer[0].subresourceRange.levelCount = 1;
        toTransfer[0].subresourceRange.layerCount = 1;
        toTransfer[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer[1] = toTransfer[0];
        toTransfer[1].image = target.NormalGuideImage;
        toTransfer[2] = toTransfer[0];
        toTransfer[2].image = target.MaterialGuideImage;
        toTransfer[3] = toTransfer[0];
        toTransfer[3].image = target.RigidMotionGuideImage;
        toTransfer[4] = toTransfer[0];
        toTransfer[4].image = target.AmbientGuideImage;
        toTransfer[5] = toTransfer[0];
        toTransfer[5].image = target.ShadowImage;
        toTransfer[6] = toTransfer[0];
        toTransfer[6].image = target.DepthImage;
        toTransfer[6].subresourceRange.aspectMask = NativeDepthAspect(mDepthFormat);
        toTransfer[7] = toTransfer[0];
        toTransfer[7].image = target.FogGuideImage;
        toTransfer[8] = toTransfer[7];
        toTransfer[8].image = target.OutlineGeometryGuideImage;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, static_cast<uint32_t>(toTransfer.size()), toTransfer.data());
        VkClearColorValue black{};
        const VkClearColorValue noFog{{0.0F, 0.0F, 0.0F, 1.0F}};
        vkCmdClearColorImage(command, target.FogGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &noFog, 1, &toTransfer[7].subresourceRange);
        vkCmdClearColorImage(command, target.OutlineGeometryGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &noFog, 1,
                             &toTransfer[8].subresourceRange);
        vkCmdClearColorImage(command, target.ColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &toTransfer[0].subresourceRange);
        const VkClearColorValue flatNormal{ { 0.5F, 0.5F, 1.0F, 0.0F } };
        vkCmdClearColorImage(command, target.NormalGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &flatNormal, 1,
                             &toTransfer[1].subresourceRange);
        vkCmdClearColorImage(command, target.MaterialGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &toTransfer[2].subresourceRange);
        vkCmdClearColorImage(command, target.RigidMotionGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &toTransfer[3].subresourceRange);
        const VkClearColorValue ambientFallback{
            {1.0F, 1.0F, 1.0F, 0.0F}};
        vkCmdClearColorImage(command, target.AmbientGuideImage,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &ambientFallback, 1,
                             &toTransfer[4].subresourceRange);
        VkClearColorValue emptyShadow{};
        emptyShadow.uint32[0] = 0xFFFFFFFFU;
        vkCmdClearColorImage(command, target.ShadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &emptyShadow, 1,
                             &toTransfer[5].subresourceRange);
        const VkClearDepthStencilValue clearDepth{ 1.0F, 0U };
        vkCmdClearDepthStencilImage(command, target.DepthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDepth, 1,
                                    &toTransfer[6].subresourceRange);
        std::array<VkImageMemoryBarrier, 9> toAttachment = toTransfer;
        toAttachment[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toAttachment[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toAttachment[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toAttachment[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toAttachment[1] = toAttachment[0];
        toAttachment[7] = toAttachment[0];
        toAttachment[7].image = target.FogGuideImage;
        toAttachment[8] = toAttachment[7];
        toAttachment[8].image = target.OutlineGeometryGuideImage;
        toAttachment[1].image = target.NormalGuideImage;
        toAttachment[2] = toAttachment[0];
        toAttachment[2].image = target.MaterialGuideImage;
        toAttachment[3] = toAttachment[0];
        toAttachment[3].image = target.RigidMotionGuideImage;
        toAttachment[4].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toAttachment[4].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toAttachment[4].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toAttachment[4].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toAttachment[5].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toAttachment[5].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toAttachment[5].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toAttachment[5].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toAttachment[6].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toAttachment[6].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toAttachment[6].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toAttachment[6].dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(toAttachment.size()),
                             toAttachment.data());
        EndImmediateCommands(command);
        if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            command = BeginImmediateCommands();
            std::array<VkImageMemoryBarrier, 8> msaaBarriers{};
            const std::array<VkImage, 8> msaaImages{
                target.MsaaColorImage,         target.MsaaNormalGuideImage,
                target.MsaaMaterialGuideImage, target.MsaaRigidMotionGuideImage,
                target.MsaaAmbientGuideImage,  target.MsaaDepthImage,
                target.MsaaFogGuideImage,      target.MsaaOutlineGeometryGuideImage
            };
            for (uint32_t index = 0; index < msaaBarriers.size(); ++index) {
                auto& barrier = msaaBarriers[index];
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = msaaImages[index];
                barrier.subresourceRange.aspectMask =
                    index == 5U ? NativeDepthAspect(mDepthFormat) : VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1U;
                barrier.subresourceRange.layerCount = 1U;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, static_cast<uint32_t>(msaaBarriers.size()), msaaBarriers.data());
            vkCmdClearColorImage(command, target.MsaaColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                                 &msaaBarriers[0].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaNormalGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &flatNormal, 1, &msaaBarriers[1].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaMaterialGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black,
                                 1, &msaaBarriers[2].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaRigidMotionGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &black, 1, &msaaBarriers[3].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaAmbientGuideImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &ambientFallback, 1,
                                 &msaaBarriers[4].subresourceRange);
            vkCmdClearDepthStencilImage(command, target.MsaaDepthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        &clearDepth, 1, &msaaBarriers[5].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaFogGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &noFog, 1, &msaaBarriers[6].subresourceRange);
            vkCmdClearColorImage(command, target.MsaaOutlineGeometryGuideImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &noFog, 1, &msaaBarriers[7].subresourceRange);
            for (uint32_t index = 0; index < msaaBarriers.size(); ++index) {
                auto& barrier = msaaBarriers[index];
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = index == 5U ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask =
                    index == 5U
                        ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
            vkCmdPipelineBarrier(
                command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                nullptr, 0, nullptr, static_cast<uint32_t>(msaaBarriers.size()), msaaBarriers.data());
            EndImmediateCommands(command);
        }
    } else {
        VkCommandBuffer command = BeginImmediateCommands();
        std::array<VkImageMemoryBarrier, 3> barriers{};
        const std::array<VkImage, 3> images{
            target.ColorImage, target.ShadowImage, target.DepthImage};
        for (uint32_t index = 0; index < barriers.size(); ++index) {
            auto& barrier = barriers[index];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = images[index];
            barrier.subresourceRange.aspectMask =
                index == 2U ? NativeDepthAspect(mDepthFormat) : VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1U;
            barrier.subresourceRange.layerCount = 1U;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        const VkClearColorValue black{};
        vkCmdClearColorImage(command, target.ColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                             &barriers[0].subresourceRange);
        VkClearColorValue emptyShadow{};
        emptyShadow.uint32[0] = 0xFFFFFFFFU;
        vkCmdClearColorImage(command, target.ShadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &emptyShadow, 1,
                             &barriers[1].subresourceRange);
        const VkClearDepthStencilValue clearDepth{ 1.0F, 0U };
        vkCmdClearDepthStencilImage(command, target.DepthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDepth, 1,
                                    &barriers[2].subresourceRange);
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barriers[2].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[2].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[2].dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        EndImmediateCommands(command);

        if (mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            command = BeginImmediateCommands();
            std::array<VkImageMemoryBarrier, 2> msaaBarriers{};
            const std::array<VkImage, 2> msaaImages{
                target.MsaaColorImage, target.MsaaDepthImage};
            for (uint32_t index = 0; index < msaaBarriers.size(); ++index) {
                auto& barrier = msaaBarriers[index];
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = msaaImages[index];
                barrier.subresourceRange.aspectMask =
                    index == 1U ? NativeDepthAspect(mDepthFormat) : VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1U;
                barrier.subresourceRange.layerCount = 1U;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, static_cast<uint32_t>(msaaBarriers.size()), msaaBarriers.data());
            vkCmdClearColorImage(command, target.MsaaColorImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                                 &msaaBarriers[0].subresourceRange);
            vkCmdClearDepthStencilImage(command, target.MsaaDepthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        &clearDepth, 1, &msaaBarriers[1].subresourceRange);
            for (uint32_t index = 0; index < msaaBarriers.size(); ++index) {
                auto& barrier = msaaBarriers[index];
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = index == 1U ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask =
                    index == 1U
                        ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
            vkCmdPipelineBarrier(
                command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                nullptr, 0, nullptr, static_cast<uint32_t>(msaaBarriers.size()), msaaBarriers.data());
            EndImmediateCommands(command);
        }
    }

    auto& inserted = targets.emplace(key, std::move(target))
                         .first->second;
    const auto publishSurface = [&](VkImage image, VkFormat format,
                                    VkImageUsageFlags usage,
                                    Extension::SceneSurfaceKind kind,
                                    uint32_t physicalAddress) -> uint64_t {
        if (image == VK_NULL_HANDLE)
            return 0U;
        const uintptr_t nativeImage = reinterpret_cast<uintptr_t>(image);
        const auto& surface = NativePicaSurfaceRegistry(inserted).Publish(
            { { key.RenderTargetNamespace, physicalAddress, kind },
              nativeImage, inserted.Width, inserted.Height,
              static_cast<uint32_t>(format), 0, true });
        mResourceStates.Transition(nativeImage,
            { kind == Extension::SceneSurfaceKind::Depth
                  ? Extension::ResourceAccess::DepthAttachment
                  : Extension::ResourceAccess::ColorAttachment,
              mGraphicsQueueFamily });
        mNriInterop.WrapTexture(image, format, VK_IMAGE_TYPE_2D, usage,
                                inserted.Width, inserted.Height);
        return surface.Generation;
    };
    inserted.ColorSurfaceGeneration = publishSurface(
        inserted.ColorImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        Extension::SceneSurfaceKind::Color, key.ColorPhysicalAddress);
    inserted.DepthSurfaceGeneration = publishSurface(
        inserted.DepthImage, mDepthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        Extension::SceneSurfaceKind::Depth, key.DepthPhysicalAddress);
    publishSurface(inserted.NormalGuideImage, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::NormalGuide,
                   key.ColorPhysicalAddress);
    publishSurface(inserted.MaterialGuideImage, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::MaterialGuide,
                   key.ColorPhysicalAddress);
    publishSurface(inserted.RigidMotionGuideImage,
                   VK_FORMAT_R16G16B16A16_SFLOAT,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::RigidMotionGuide,
                   key.ColorPhysicalAddress);
    publishSurface(inserted.AmbientGuideImage,
                   VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::AmbientGuide,
                   key.ColorPhysicalAddress);
    publishSurface(inserted.FogGuideImage, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::FogGuide, key.ColorPhysicalAddress);
    publishSurface(inserted.OutlineGeometryGuideImage, VK_FORMAT_R32G32B32A32_UINT,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   Extension::SceneSurfaceKind::OutlineGeometryGuide, key.ColorPhysicalAddress);
    mInvalidatedNativePicaRenderTargetAddresses.erase(
        {key.RenderTargetNamespace, key.ColorPhysicalAddress});
    return inserted;
}

void GfxRenderingAPIVulkan::DestroyNativePicaRenderTarget(
    NativePicaRenderTarget& target) {
    for (VkImage image :
         { target.ColorImage, target.DepthImage, target.NormalGuideImage, target.MaterialGuideImage,
           target.RigidMotionGuideImage, target.AmbientGuideImage, target.FogGuideImage,
           target.OutlineGeometryGuideImage, target.ShadowImage, target.MsaaColorImage, target.MsaaNormalGuideImage,
           target.MsaaMaterialGuideImage, target.MsaaRigidMotionGuideImage, target.MsaaAmbientGuideImage,
           target.MsaaFogGuideImage, target.MsaaOutlineGeometryGuideImage, target.MsaaDepthImage,
           target.DiffuseAlbedoGuideImage, target.MsaaDiffuseAlbedoGuideImage }) {
        if (image == VK_NULL_HANDLE) continue;
        mNriPicaDisplayCopyPass.ForgetTexture(image);
        mResourceStates.Forget(reinterpret_cast<uintptr_t>(image));
        if (!target.NriOwned) mNriInterop.ForgetTexture(image);
    }
    auto& surfaces = NativePicaSurfaceRegistry(target);
    surfaces.Retire({ target.Key.RenderTargetNamespace, target.Key.DepthPhysicalAddress,
                      Extension::SceneSurfaceKind::Depth });
    for (const auto kind : {Extension::SceneSurfaceKind::Color,
            Extension::SceneSurfaceKind::NormalGuide, Extension::SceneSurfaceKind::MaterialGuide,
            Extension::SceneSurfaceKind::RigidMotionGuide, Extension::SceneSurfaceKind::AmbientGuide,
            Extension::SceneSurfaceKind::FogGuide, Extension::SceneSurfaceKind::OutlineGeometryGuide,
            Extension::SceneSurfaceKind::DiffuseAlbedoGuide})
        surfaces.Retire({ target.Key.RenderTargetNamespace, target.Key.ColorPhysicalAddress, kind });
    if (target.Framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(mDevice, target.Framebuffer, nullptr);
    }
    if (target.CanonicalFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(mDevice, target.CanonicalFramebuffer, nullptr);
    }
    if (target.ColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.ColorView, nullptr);
    }
    if (target.NormalGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.NormalGuideView, nullptr);
    }
    if (target.MaterialGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.MaterialGuideView, nullptr);
    }
    if (target.RigidMotionGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.RigidMotionGuideView, nullptr);
    }
    if (target.AmbientGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.AmbientGuideView, nullptr);
    }
    if (target.FogGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.FogGuideView, nullptr);
    }
    if (target.OutlineGeometryGuideView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.OutlineGeometryGuideView, nullptr);
    }
    if (target.ShadowView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.ShadowView, nullptr);
    }
    if (target.DepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(mDevice, target.DepthView, nullptr);
    }
    for (VkImageView view : { target.MsaaColorView, target.MsaaNormalGuideView, target.MsaaMaterialGuideView,
                              target.MsaaRigidMotionGuideView, target.MsaaAmbientGuideView, target.MsaaFogGuideView,
                              target.MsaaOutlineGeometryGuideView, target.MsaaDepthView,
                              target.DiffuseAlbedoGuideView, target.MsaaDiffuseAlbedoGuideView }) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(mDevice, view, nullptr);
    }
    // The albedo guide is grown outside the target owner's image set.
    for (const auto& [image, memory] :
         { std::pair{ target.DiffuseAlbedoGuideImage, target.DiffuseAlbedoGuideMemory },
           std::pair{ target.MsaaDiffuseAlbedoGuideImage, target.MsaaDiffuseAlbedoGuideMemory } }) {
        if (image == VK_NULL_HANDLE) continue;
        if (target.NriOwned) {
            mNriInterop.DestroyOwnedTexture(image);
        } else {
            vkDestroyImage(mDevice, image, nullptr);
            if (memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, memory, nullptr);
        }
    }
    if (target.NriOwned) {
        mNriPicaRenderTargetOwner.Destroy({
            target.ColorImage,
            target.NormalGuideImage,
            target.MaterialGuideImage,
            target.RigidMotionGuideImage,
            target.AmbientGuideImage,
            target.FogGuideImage,
            target.OutlineGeometryGuideImage,
            target.ShadowImage,
            target.DepthImage,
            target.MsaaColorImage,
            target.MsaaNormalGuideImage,
            target.MsaaMaterialGuideImage,
            target.MsaaRigidMotionGuideImage,
            target.MsaaAmbientGuideImage,
            target.MsaaFogGuideImage,
            target.MsaaOutlineGeometryGuideImage,
            target.MsaaDepthImage,
        });
    } else {
        for (VkImage image :
             { target.ColorImage, target.NormalGuideImage, target.MaterialGuideImage, target.RigidMotionGuideImage,
               target.AmbientGuideImage, target.FogGuideImage, target.OutlineGeometryGuideImage, target.ShadowImage,
               target.DepthImage, target.MsaaColorImage, target.MsaaNormalGuideImage, target.MsaaMaterialGuideImage,
               target.MsaaRigidMotionGuideImage, target.MsaaAmbientGuideImage, target.MsaaFogGuideImage,
               target.MsaaOutlineGeometryGuideImage, target.MsaaDepthImage }) {
            if (image != VK_NULL_HANDLE)
                vkDestroyImage(mDevice, image, nullptr);
        }
        for (VkDeviceMemory memory :
             { target.ColorMemory, target.NormalGuideMemory, target.MaterialGuideMemory, target.RigidMotionGuideMemory,
               target.AmbientGuideMemory, target.FogGuideMemory, target.OutlineGeometryGuideMemory, target.ShadowMemory,
               target.DepthMemory, target.MsaaColorMemory, target.MsaaNormalGuideMemory, target.MsaaMaterialGuideMemory,
               target.MsaaRigidMotionGuideMemory, target.MsaaAmbientGuideMemory, target.MsaaFogGuideMemory,
               target.MsaaOutlineGeometryGuideMemory, target.MsaaDepthMemory }) {
            if (memory != VK_NULL_HANDLE)
                vkFreeMemory(mDevice, memory, nullptr);
        }
    }
    target = {};
}

GfxRenderingAPIVulkan::NativePicaDisplayImage&
GfxRenderingAPIVulkan::GetOrCreateNativePicaDisplayImage(
    uint64_t renderTargetNamespace, uint32_t physicalAddress,
    uint32_t width, uint32_t height) {
    return GetOrCreateNativePicaDisplayImage(
        mNativePicaDisplayImages, renderTargetNamespace, physicalAddress,
        width, height);
}

GfxRenderingAPIVulkan::NativePicaRenderTarget&
GfxRenderingAPIVulkan::ResolveNativePicaUiTarget(NativePicaRenderTarget& scene) {
    const auto found = mNativePicaUiTargets.find(scene.Key);
    if (found != mNativePicaUiTargets.end() && Extension::NativeUiCompanionCurrent(
        scene.ContentWriteSerial, found->second.UiSourceSerial,
        found->second.ContentWriteSerial, found->second.PresentationEffectsResolvedSerial))
        return found->second;
    return scene;
}

void GfxRenderingAPIVulkan::DestroyNativePicaUiTargets() {
    for (auto& [key, target] : mNativePicaUiTargets)
        DestroyNativePicaRenderTarget(target);
    mNativePicaUiTargets.clear();
}

void GfxRenderingAPIVulkan::CopyNativePicaUiDepth(
    const NativePicaRenderTarget& source, NativePicaRenderTarget& destination) {
    // Upscaler modes are single-sampled. Preserve native depth AND stencil;
    // clearing either would change depth-tested UI/isolated overlay semantics.
    EndNativePicaRenderPass();
    const auto command = mCommandBuffers[mCurrentFrame];
    std::array<VkImageMemoryBarrier, 2> barriers{};
    for (auto& barrier : barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {NativeDepthAspect(mDepthFormat), 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    barriers[0].image = source.DepthImage;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].image = destination.DepthImage;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers.data());
    for (const auto aspect : {VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT}) {
        if ((NativeDepthAspect(mDepthFormat) & aspect) == 0) continue;
        VkImageBlit blit{};
        blit.srcSubresource = {static_cast<VkImageAspectFlags>(aspect), 0, 0, 1};
        blit.dstSubresource = blit.srcSubresource;
        blit.srcOffsets[1] = {int32_t(source.Width), int32_t(source.Height), 1};
        blit.dstOffsets[1] = {int32_t(destination.Width), int32_t(destination.Height), 1};
        vkCmdBlitImage(command, source.DepthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination.DepthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    }
    for (auto& barrier : barriers) {
        std::swap(barrier.oldLayout, barrier.newLayout);
        std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers.data());
}

void GfxRenderingAPIVulkan::DestroyNativePicaSceneBeforeUiImages() {
    DestroyNativePicaUiTargets();
    for (auto* images : {&mNativePicaSceneBeforeUiTargets,
                         &mNativePicaSceneBeforeUiDisplays}) {
        for (auto& [key, image] : *images) {
            DestroyNativePicaDisplayImage(image);
        }
        images->clear();
    }
}

GfxRenderingAPIVulkan::NativePicaDisplayImage&
GfxRenderingAPIVulkan::GetOrCreateNativePicaDisplayImage(
    std::map<std::pair<uint64_t, uint32_t>, NativePicaDisplayImage>& images,
    uint64_t renderTargetNamespace, uint32_t physicalAddress,
    uint32_t width, uint32_t height) {
    const auto key = std::make_pair(renderTargetNamespace, physicalAddress);
    auto found = images.find(key);
    if (found != images.end() &&
        found->second.Width == width && found->second.Height == height) {
        return found->second;
    }
    if (found != images.end()) {
        // Commands already recorded in this frame, and the previous frame
        // still in flight, may sample or write the replaced image (a guest
        // that alternates crop and scaled transfers into one address resizes
        // it within a frame). Destroy it once this slot's fence completes.
        if (mFrameActive) {
            mRetiredNativePicaDisplayImages[mCurrentFrame].push_back(std::move(found->second));
        } else {
            DestroyNativePicaDisplayImage(found->second);
        }
        images.erase(found);
    }

    NativePicaDisplayImage image;
    image.Width = width;
    image.Height = height;
    Extension::NriPicaTextureImage nriImage;
    if (mNriPicaDisplayImageOwner.Create(
            width, height, VK_FORMAT_R8G8B8A8_UNORM, 1U, nriImage,
            true)) {
        image.Image = nriImage.Image;
        image.View = nriImage.View;
        image.NriOwned = true;
        mDiagnostics.RecordNriPicaDisplayImage(true);
    } else {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckNativeVk(
            vkCreateImage(mDevice, &imageInfo, nullptr, &image.Image),
            "vkCreateImage(native PICA display transfer)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, image.Image, &requirements);
        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckNativeVk(
            vkAllocateMemory(
                mDevice, &allocation, nullptr, &image.Memory),
            "vkAllocateMemory(native PICA display transfer)");
        CheckNativeVk(
            vkBindImageMemory(mDevice, image.Image, image.Memory, 0),
            "vkBindImageMemory(native PICA display transfer)");
        VkImageViewCreateInfo view{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        CheckNativeVk(
            vkCreateImageView(mDevice, &view, nullptr, &image.View),
            "vkCreateImageView(native PICA display transfer)");
    }
    return images.emplace(key, std::move(image)).first->second;
}

void GfxRenderingAPIVulkan::ReleaseRetiredNativePicaDisplayImages(uint32_t frameIndex) {
    auto& retired = mRetiredNativePicaDisplayImages[frameIndex % kFramesInFlight];
    for (auto& image : retired) {
        DestroyNativePicaDisplayImage(image);
    }
    retired.clear();
}

void GfxRenderingAPIVulkan::DestroyNativePicaDisplayImage(
    NativePicaDisplayImage& image) {
    mNriPicaDisplayCopyPass.ForgetTexture(image.Image);
    if (image.NriOwned) {
        mNriPicaDisplayImageOwner.Destroy(image.Image);
    } else {
        if (image.Image != VK_NULL_HANDLE)
            mNriInterop.ForgetTexture(image.Image);
        if (image.View != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, image.View, nullptr);
        }
        if (image.Image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, image.Image, nullptr);
        }
        if (image.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, image.Memory, nullptr);
        }
    }
    image = {};
}

void GfxRenderingAPIVulkan::BeginNativePicaRenderPass(
    NativePicaRenderTarget& target) {
    PrepareNativePicaTargetAttachments(target);
    const auto attachments = ActivePicaAttachmentRequirements();
    if (mNativePicaRenderPassActive &&
        mActiveNativePicaRenderTarget == &target &&
        mActiveNativePicaAttachments == attachments) {
        return;
    }
    EndNativePicaRenderPass();
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    if (mPicaDynamicRenderingScope.Available()) {
        const bool multisampled =
            mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT;
        Extension::PicaDynamicRenderingTarget renderingTarget;
        renderingTarget.Colors =
            multisampled
                ? std::array<VkImageView, Extension::kPicaColorAttachmentCount>{ target.MsaaColorView,
                                                                             target.MsaaNormalGuideView,
                                                                             target.MsaaMaterialGuideView,
                                                                             target.MsaaRigidMotionGuideView,
                                                                             target.MsaaAmbientGuideView,
                                                                             target.MsaaFogGuideView,
                                                                             target.MsaaOutlineGeometryGuideView,
                                                                             target.MsaaDiffuseAlbedoGuideView }
                : std::array<VkImageView, Extension::kPicaColorAttachmentCount>{ target.ColorView,
                                                                             target.NormalGuideView,
                                                                             target.MaterialGuideView,
                                                                             target.RigidMotionGuideView,
                                                                             target.AmbientGuideView,
                                                                             target.FogGuideView,
                                                                             target.OutlineGeometryGuideView,
                                                                             target.DiffuseAlbedoGuideView };
        renderingTarget.Resolves =
            multisampled ? std::array<VkImageView, Extension::kPicaColorAttachmentCount>{ target.ColorView,
                                                                                      target.NormalGuideView,
                                                                                      target.MaterialGuideView,
                                                                                      target.RigidMotionGuideView,
                                                                                      target.AmbientGuideView,
                                                                                      target.FogGuideView,
                                                                                      target.OutlineGeometryGuideView,
                                                                                      target.DiffuseAlbedoGuideView }
                         : std::array<VkImageView, Extension::kPicaColorAttachmentCount>{};
        renderingTarget.Depth =
            multisampled ? target.MsaaDepthView : target.DepthView;
        renderingTarget.DepthResolve =
            multisampled ? target.DepthView : VK_NULL_HANDLE;
        renderingTarget.ColorImages =
            multisampled ? std::array<VkImage, Extension::kPicaColorAttachmentCount>{ target.MsaaColorImage,
                                                                                  target.MsaaNormalGuideImage,
                                                                                  target.MsaaMaterialGuideImage,
                                                                                  target.MsaaRigidMotionGuideImage,
                                                                                  target.MsaaAmbientGuideImage,
                                                                                  target.MsaaFogGuideImage,
                                                                                  target.MsaaOutlineGeometryGuideImage,
                                                                                  target.MsaaDiffuseAlbedoGuideImage }
                         : std::array<VkImage, Extension::kPicaColorAttachmentCount>{ target.ColorImage,
                                                                                  target.NormalGuideImage,
                                                                                  target.MaterialGuideImage,
                                                                                  target.RigidMotionGuideImage,
                                                                                  target.AmbientGuideImage,
                                                                                  target.FogGuideImage,
                                                                                  target.OutlineGeometryGuideImage,
                                                                                  target.DiffuseAlbedoGuideImage };
        renderingTarget.ResolveImages =
            multisampled ? std::array<VkImage, Extension::kPicaColorAttachmentCount>{ target.ColorImage,
                                                                                  target.NormalGuideImage,
                                                                                  target.MaterialGuideImage,
                                                                                  target.RigidMotionGuideImage,
                                                                                  target.AmbientGuideImage,
                                                                                  target.FogGuideImage,
                                                                                  target.OutlineGeometryGuideImage,
                                                                                  target.DiffuseAlbedoGuideImage }
                         : std::array<VkImage, Extension::kPicaColorAttachmentCount>{};
        renderingTarget.DepthImage =
            multisampled ? target.MsaaDepthImage : target.DepthImage;
        renderingTarget.DepthResolveImage =
            multisampled ? target.DepthImage : VK_NULL_HANDLE;
        renderingTarget.ColorFormats = Extension::BoundPicaColorAttachmentFormats(attachments);
        // The guides no effect requires this frame stay allocated but unbound.
        for (size_t index = 0; index < renderingTarget.ColorFormats.size(); ++index) {
            if (renderingTarget.ColorFormats[index] != VK_FORMAT_UNDEFINED) continue;
            renderingTarget.Colors[index] = renderingTarget.Resolves[index] = VK_NULL_HANDLE;
            renderingTarget.ColorImages[index] = renderingTarget.ResolveImages[index] = VK_NULL_HANDLE;
        }
        renderingTarget.DepthFormat = mDepthFormat;
        renderingTarget.ColorAttachmentCount = attachments.ColorAttachmentCount();
        renderingTarget.Width = target.Width;
        renderingTarget.Height = target.Height;
        renderingTarget.Samples = mNativePicaSampleCount;
        if (!mPicaDynamicRenderingScope.Begin(
                mCurrentFrame, mCommandBuffers[mCurrentFrame],
                renderingTarget))
            throw std::runtime_error(
                "native PICA dynamic rendering scope failed");
        mDiagnostics.RecordNativePicaDynamicRendering(
            mPicaDynamicRenderingScope.ActiveOwnedByNri());
    } else {
        VkRenderPassBeginInfo begin{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = attachments.NativeColorOnly()
            ? mNativePicaCanonicalRenderPass
            : mNativePicaRenderPass;
        begin.framebuffer = NativePicaFramebuffer(target, attachments.NativeColorOnly());
        begin.renderArea.extent = {target.Width, target.Height};
        vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &begin,
                             VK_SUBPASS_CONTENTS_INLINE);
    }
    mGpuProfiler.BeginScope(
        mCurrentFrame, Extension::GpuProfileScope::NativePica,
        mCommandBuffers[mCurrentFrame]);
    if (mGpuProfiler.TimelineEnabled()) {
        mGpuProfiler.MarkNamed(mCurrentFrame,
                               "pica ns" + std::to_string(target.Key.RenderTargetNamespace) + " " +
                                   std::to_string(target.Width) + "x" + std::to_string(target.Height) +
                                   (target.NormalGuideView != VK_NULL_HANDLE ? " guides" : ""),
                               mCommandBuffers[mCurrentFrame]);
    }
    if (mFrameGraphicsSettings.Effects.Toon !=
        Extension::ToonMode::Off) {
        mGpuProfiler.BeginScope(
            mCurrentFrame, Extension::GpuProfileScope::ToonRaster,
            mCommandBuffers[mCurrentFrame]);
    }
    mNativePicaRenderPassActive = true;
    mActiveNativePicaRenderTarget = &target;
    mActiveNativePicaAttachments = attachments;
}

void GfxRenderingAPIVulkan::EndNativePicaRenderPass() {
    if (!mNativePicaRenderPassActive) {
        return;
    }
    if (mPicaDynamicRenderingScope.Active()) {
        mPicaDynamicRenderingScope.End(
            mCurrentFrame, mCommandBuffers[mCurrentFrame]);
        mDiagnostics.RecordNriPicaGlobalBarriers(
            mPicaDynamicRenderingScope.LastNriGlobalBarrierCount());
    } else {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
    }
    mGpuProfiler.EndScope(
        mCurrentFrame, Extension::GpuProfileScope::ToonRaster,
        mCommandBuffers[mCurrentFrame]);
    mGpuProfiler.EndScope(
        mCurrentFrame, Extension::GpuProfileScope::NativePica,
        mCommandBuffers[mCurrentFrame]);
    mNativePicaRenderPassActive = false;
    mActiveNativePicaRenderTarget = nullptr;
}

void GfxRenderingAPIVulkan::ApplyPendingNativePicaMemoryFills(NativePicaRenderTarget& target) {
    const uint64_t pixelCount = static_cast<uint64_t>(target.Key.Width) * target.Key.Height;
    const uint64_t colorEnd = static_cast<uint64_t>(target.Key.ColorPhysicalAddress) +
                              pixelCount * NativePicaColorBytesPerPixel(target.Key.ColorFormat);
    const uint64_t depthEnd = static_cast<uint64_t>(target.Key.DepthPhysicalAddress) +
                              pixelCount * NativePicaDepthBytesPerPixel(target.Key.DepthFormat);
    if (!mPicaMemoryFillSmokeInjected) {
        GfxNativePicaMemoryFillView smokeFill;
        if (BuildNativePicaMemoryFillSmoke(
                target.Key.RenderTargetNamespace,
                target.Key.ColorPhysicalAddress,
                target.Key.DepthPhysicalAddress, colorEnd, depthEnd,
                smokeFill)) {
            mPendingNativePicaMemoryFills.push_back(smokeFill);
            mPicaMemoryFillSmokeInjected = true;
            mDiagnostics.RecordMemoryFill();
        }
    }
    if (mPendingNativePicaMemoryFills.empty()) {
        return;
    }

    auto fill = mPendingNativePicaMemoryFills.begin();
    while (fill != mPendingNativePicaMemoryFills.end()) {
        if (fill->RenderTargetNamespace != target.Key.RenderTargetNamespace) {
            ++fill;
            continue;
        }
        const bool coversColor =
            fill->StartPhysicalAddress <= target.Key.ColorPhysicalAddress && fill->EndPhysicalAddress >= colorEnd;
        const bool coversDepth =
            fill->StartPhysicalAddress <= target.Key.DepthPhysicalAddress && fill->EndPhysicalAddress >= depthEnd;
        if (!coversColor && !coversDepth) {
            ++fill;
            continue;
        }
        bool nriShadowClear = false;
        uint32_t shadowBarrierCount = 0;
        if (coversColor && target.ShadowImage != VK_NULL_HANDLE) {
            // The shadow clear is transfer/compute work: no render pass may be
            // active, neither the PICA one nor the swapchain/overlay pass. A
            // clear recorded inside the latter is invalid; on NVIDIA it wrote
            // past another image once targets were scaled (device lost).
            EndNativePicaRenderPass();
            if (mRenderPassActive) {
                vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
                mRenderPassActive = false;
                mOverlayRenderPassActive = false;
            }
            const uint32_t shadowClearValue = static_cast<uint32_t>(NativePicaFillByte(*fill, 0U)) |
                                              (static_cast<uint32_t>(NativePicaFillByte(*fill, 1U)) << 8U) |
                                              (static_cast<uint32_t>(NativePicaFillByte(*fill, 2U)) << 16U) |
                                              (static_cast<uint32_t>(NativePicaFillByte(*fill, 3U)) << 24U);
            nriShadowClear = mNriPicaMemoryFillClearPass.ClearShadow({
                mCurrentFrame,
                target.ShadowImage,
                target.Width,
                target.Height,
                shadowClearValue,
            });
            if (nriShadowClear) {
                shadowBarrierCount = mNriPicaMemoryFillClearPass.LastShadowBarrierCount();
            } else {
                VkImageMemoryBarrier toClear{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                toClear.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toClear.image = target.ShadowImage;
                toClear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                toClear.subresourceRange.levelCount = 1U;
                toClear.subresourceRange.layerCount = 1U;
                toClear.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                auto command = mCommandBuffers[mCurrentFrame];
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &toClear);
                VkClearColorValue shadowValue{};
                shadowValue.uint32[0] = shadowClearValue;
                vkCmdClearColorImage(command, target.ShadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &shadowValue, 1,
                                     &toClear.subresourceRange);
                std::swap(toClear.oldLayout, toClear.newLayout);
                toClear.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toClear.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &toClear);
            }
        }
        BeginNativePicaRenderPass(target);
        const VkClearColorValue decodedColor = DecodeNativePicaClearColor(*fill, target.Key.ColorFormat);
        const VkClearDepthStencilValue decodedDepth = DecodeNativePicaClearDepthStencil(*fill, target.Key.DepthFormat);
        Extension::NriPicaAttachmentClearDesc nriClear;
        nriClear.FrameIndex = mCurrentFrame;
        nriClear.Width = target.Width;
        nriClear.Height = target.Height;
        nriClear.Color = { decodedColor.float32[0], decodedColor.float32[1], decodedColor.float32[2],
                           decodedColor.float32[3] };
        nriClear.Depth = decodedDepth.depth;
        nriClear.Stencil = static_cast<uint8_t>(decodedDepth.stencil);
        nriClear.ClearColor = coversColor;
        nriClear.ClearDepth = coversDepth;
        nriClear.HasStencil = target.Key.DepthFormat == 3U;
        nriClear.NriRenderingScope = mPicaDynamicRenderingScope.ActiveOwnedByNri();
        nriClear.Attachments = mActiveNativePicaAttachments;
        const bool nriAttachmentClear = mNriPicaMemoryFillClearPass.ClearAttachments(nriClear);
        if (!nriAttachmentClear) {
            std::array<VkClearAttachment, Extension::kPicaColorAttachmentCount + 1U> attachments{};
            uint32_t attachmentCount = 0;
            if (coversColor) {
                auto& attachment = attachments[attachmentCount++];
                attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                attachment.colorAttachment = 0U;
                attachment.clearValue.color = decodedColor;
            }
            // The bound guides' cleared values; unbound guides are not in the pass.
            const std::array<VkClearColorValue, Extension::kPicaColorAttachmentCount> guideClears{ {
                {}, { { 0.5F, 0.5F, 1.0F, 0.0F } }, {}, {}, { { 1.0F, 1.0F, 1.0F, 0.0F } },
                { { 0.0F, 0.0F, 0.0F, 1.0F } }, { { 0.0F, 0.0F, 0.0F, 1.0F } }, {} } };
            for (uint32_t index = 1U; coversColor && index < guideClears.size(); ++index) {
                if (!mActiveNativePicaAttachments.Binds(static_cast<Extension::PicaColorAttachment>(index))) continue;
                auto& guide = attachments[attachmentCount++];
                guide.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                guide.colorAttachment = index;
                guide.clearValue.color = guideClears[index];
            }
            if (coversDepth) {
                auto& attachment = attachments[attachmentCount++];
                attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                if (target.Key.DepthFormat == 3U) {
                    attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
                attachment.clearValue.depthStencil = decodedDepth;
            }
            const VkClearRect clearRect{ { { 0, 0 }, { target.Width, target.Height } }, 0U, 1U };
            vkCmdClearAttachments(mCommandBuffers[mCurrentFrame], attachmentCount, attachments.data(), 1U, &clearRect);
        }
        mDiagnostics.RecordNriPicaMemoryFillClear(nriShadowClear, nriAttachmentClear, shadowBarrierCount);
        if (coversColor) {
            // Filled colour is new content: it carries no resolved scene
            // effects any more, so the provenance serial moves on.
            target.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
        }
        fill = mPendingNativePicaMemoryFills.erase(fill);
    }
}

void GfxRenderingAPIVulkan::PaintNativePicaPictureBars(
    const Renderer3ds::PicaCanvasSurround& bars) {
    if (bars.Count == 0U) {
        return;
    }
    // Opaque black on the native colour only: the bars are presentation
    // pixels, so the guides, depth and stencil of what lies under them stay.
    Extension::NriPicaColorRectsClearDesc clear;
    clear.FrameIndex = mCurrentFrame;
    clear.Color = {0.0F, 0.0F, 0.0F, 1.0F};
    clear.RectCount = bars.Count;
    for (uint32_t index = 0; index < bars.Count; ++index) {
        const auto& rect = bars.Rects[index];
        clear.Rects[index] = {static_cast<uint32_t>(rect.X), static_cast<uint32_t>(rect.Y),
                              rect.Width, rect.Height};
    }
    clear.NriRenderingScope = mPicaDynamicRenderingScope.ActiveOwnedByNri();
    if (mNriPicaMemoryFillClearPass.ClearColorRects(clear)) {
        return;
    }
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0U;
    attachment.clearValue.color = {{0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<VkClearRect, 4> rects{};
    for (uint32_t index = 0; index < bars.Count; ++index) {
        const auto& rect = bars.Rects[index];
        rects[index] = {{{rect.X, rect.Y}, {rect.Width, rect.Height}}, 0U, 1U};
    }
    vkCmdClearAttachments(mCommandBuffers[mCurrentFrame], 1U, &attachment, bars.Count,
                          rects.data());
}

VkDescriptorSet GfxRenderingAPIVulkan::AllocateNativePicaDescriptorSet() {
    VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = mFrameResources[mCurrentFrame].DescriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &mNativePicaDescriptorSetLayout;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    CheckNativeVk(vkAllocateDescriptorSets(mDevice, &allocation, &descriptor),
                  "vkAllocateDescriptorSets(native PICA)");
    return descriptor;
}

VkDescriptorSet
GfxRenderingAPIVulkan::AllocateNativePicaScanoutDescriptorSet() {
    VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = mFrameResources[mCurrentFrame].DescriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &mNativePicaScanoutDescriptorSetLayout;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    CheckNativeVk(vkAllocateDescriptorSets(mDevice, &allocation, &descriptor),
                  "vkAllocateDescriptorSets(native PICA scanout)");
    return descriptor;
}

void GfxRenderingAPIVulkan::ForgetNativePicaEffectNriTextures() {
    for (VkImage image : {
            mMotionVectorPass.OutputImage(),
            mMotionVectorPass.ReactiveImage()}) {
        if (image != VK_NULL_HANDLE) mNriInterop.ForgetTexture(image);
    }
    // Temporal AA histories are committed NRI textures and are destroyed by
    // the pass through the owned-resource path.
}

void GfxRenderingAPIVulkan::PublishGlobalIlluminationStatus(Extension::GlobalIlluminationMode mode) {
    // Only the screen-space provider reports a readiness of its own (its
    // pipelines are created off the frame).
    Extension::GlobalIlluminationProviderStatus status{mode, false, {}};
    if (mode == Extension::GlobalIlluminationMode::ScreenSpace) {
        status.Ready = mScreenSpaceGi.Ready();
        if (!status.Ready) status.Reason = mScreenSpaceGi.UnavailableReason();
    }
    if (status == mPublishedGlobalIlluminationStatus) return;
    mPublishedGlobalIlluminationStatus = status;
    if (mode == Extension::GlobalIlluminationMode::ScreenSpace)
        std::fprintf(stderr, "TRIAEVUM_SCREEN_SPACE_GI ready=%d reason=\"%s\"\n", status.Ready ? 1 : 0,
                     status.Reason.c_str());
    Extension::GraphicsSettingsRuntime::Instance().PublishGlobalIlluminationStatus(std::move(status));
}

bool GfxRenderingAPIVulkan::UpscalerProviderAvailable(Extension::UpscalerProvider provider) const {
    switch (provider) {
        case Extension::UpscalerProvider::Nis:
            return mNriUpscalerPass.Available();
        case Extension::UpscalerProvider::Fsr:
            return mNriUpscalerPass.FsrAvailable();
        case Extension::UpscalerProvider::Dlss:
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
            if (mD3d12NgxProvider.Available()) return true;
#endif
            return mNriUpscalerPass.DlssAvailable();
        case Extension::UpscalerProvider::Xess:
            return false;
    }
    return false;
}

void GfxRenderingAPIVulkan::WithdrawFailedUpscaler(Extension::UpscalerProvider provider) {
    Extension::GraphicsCapability capability = Extension::GraphicsCapability::NriNisUpscaler;
    const char* name = "NIS";
    bool available = false;
    std::string reason;
    switch (provider) {
        case Extension::UpscalerProvider::Nis:
            available = mNriUpscalerPass.Available();
            reason = mNriUpscalerPass.UnavailableReason();
            break;
        case Extension::UpscalerProvider::Fsr:
            capability = Extension::GraphicsCapability::NriFsrUpscaler;
            name = "FSR";
            available = mNriUpscalerPass.FsrAvailable();
            reason = mNriInterop.FsrUnavailableReason();
            break;
        case Extension::UpscalerProvider::Dlss:
            capability = Extension::GraphicsCapability::NriDlssUpscaler;
            name = "DLSS";
            available = mNriUpscalerPass.DlssAvailable();
            reason = mNriInterop.DlssUnavailableReason();
            break;
        case Extension::UpscalerProvider::Xess:
            capability = Extension::GraphicsCapability::NriXessUpscaler;
            name = "XeSS";
            reason = "NRI XeSS is unavailable on the Vulkan backend";
            break;
    }
    if (available) reason = "its inputs were not ready";
    const std::string report = std::string(name) + ": " + reason;
    if (report != mReportedUpscalerFallback) {
        mReportedUpscalerFallback = report;
        std::fprintf(stderr, "TRIAEVUM_UPSCALER_FALLBACK provider=%s withdrawn=%d reason=\"%s\"\n", name,
                     available ? 0 : 1, reason.c_str());
        SPDLOG_WARN("{} produced no frame; the display is shown unscaled ({})", name, reason);
    }
    if (!available)
        Extension::GraphicsSettingsRuntime::Instance().SetCapability(capability, false, reason);
}

void GfxRenderingAPIVulkan::ReleaseEffectGraphImageClients() {
    mAmbientOcclusionPass.InvalidateScreenResources();
    mCacaoPass.InvalidateScreenResources();
    mMxaoPass.InvalidateScreenResources();
    mHiZDepthPyramidPass.InvalidateScreenResources();
    mFidelityFxSssrPass.InvalidateScreenResources();
    mBrixelizerGiPass.InvalidateScreenResources();
    mScreenSpaceGi.InvalidateScreenResources();
    mReflectionMaterialResolvePass.InvalidateScreenResources();
    mLinearSceneColorPass.InvalidateScreenResources();
    if (mHiZReflectionSurface.has_value()) {
        mSceneSurfaces.Retire(*mHiZReflectionSurface);
        mHiZReflectionSurface.reset();
    }
    mHiZReflectionPass.InvalidateScreenResources();
    if (mMotionSurface.has_value()) {
        mSceneSurfaces.Retire(*mMotionSurface);
        mMotionSurface.reset();
    }
    mMotionVectorPass.InvalidateScreenResources();
    if (mLinearColorSurface.has_value()) {
        mSceneSurfaces.Retire(*mLinearColorSurface);
        mLinearColorSurface.reset();
    }
    mNriUpscalerPass.InvalidateScreenResources();
    mUpscalerGuidePass.InvalidateScreenResources();
    mNativeUiOverlayPass.InvalidateScreenResources();
    if (mCompositeSurface.has_value()) {
        mSceneSurfaces.Retire(*mCompositeSurface);
        mCompositeSurface.reset();
    }
    mSceneCompositePass.InvalidateScreenResources();
    mSmaa1xPass.InvalidateScreenResources();
    if (mTaaSurface.has_value()) {
        mSceneSurfaces.Retire(*mTaaSurface);
        mTaaSurface.reset();
    }
    mTemporalAaPass.InvalidateScreenResources();
    mTaaOutputValid = false;
    mAmbientOcclusionOutputValid = false;
    mHiZOutputValid = false;
    mTemporalHistory.Reset();
    mPicaTemporalHistory.Reset();
}

void GfxRenderingAPIVulkan::ApplyInternalResolutionScale(float scale) {
    ResetNativePicaRenderTargets(true);
    mInternalResolutionScale = scale;
}

void GfxRenderingAPIVulkan::ResetNativePicaRenderTargets(bool preserveDisplayImages) {
    EndNativePicaRenderPass();
    WaitForAllPresents();
    CheckNativeVk(vkDeviceWaitIdle(mDevice),
                  "vkDeviceWaitIdle(PICA render target reconfiguration)");
    ForgetNativePicaEffectNriTextures();
    mAmbientOcclusionPass.InvalidateScreenResources();
    mCacaoPass.InvalidateScreenResources();
    mMxaoPass.InvalidateScreenResources();
    mFidelityFxSssrPass.InvalidateScreenResources();
    mBrixelizerGiPass.InvalidateScreenResources();
    mScreenSpaceGi.InvalidateScreenResources();
    mReflectionMaterialResolvePass.InvalidateScreenResources();
    mLinearSceneColorPass.InvalidateScreenResources();
    mHiZDepthPyramidPass.InvalidateScreenResources();
    if (mHiZReflectionSurface.has_value()) {
        mSceneSurfaces.Retire(*mHiZReflectionSurface);
        mHiZReflectionSurface.reset();
    }
    mHiZReflectionPass.InvalidateScreenResources();
    if (mMotionSurface.has_value()) {
        mSceneSurfaces.Retire(*mMotionSurface);
        mMotionSurface.reset();
    }
    mMotionVectorPass.InvalidateScreenResources();
    if (mLinearColorSurface.has_value()) {
        mSceneSurfaces.Retire(*mLinearColorSurface);
        mLinearColorSurface.reset();
    }
    mNriUpscalerPass.InvalidateScreenResources();
    mUpscalerGuidePass.InvalidateScreenResources();
    mNativeUiOverlayPass.InvalidateScreenResources();
    if (mCompositeSurface.has_value()) {
        mSceneSurfaces.Retire(*mCompositeSurface);
        mCompositeSurface.reset();
    }
    mSceneCompositePass.InvalidateScreenResources();
    mSmaa1xPass.InvalidateScreenResources();
    mNriEffectGraphTransientImageArena.InvalidateScreenResources();
    if (mTaaSurface.has_value()) {
        mSceneSurfaces.Retire(*mTaaSurface);
        mTaaSurface.reset();
    }
    mTemporalAaPass.InvalidateScreenResources();
    mTaaOutputValid = false;
    mHiZOutputValid = false;
    mAmbientOcclusionOutputValid = false;
    mTemporalHistory.Reset();
    mPicaTemporalHistory.Reset();
    for (auto texture = mNativePicaTextures.begin();
         texture != mNativePicaTextures.end();) {
        if (!texture->second.ImageBorrowed) {
            ++texture;
            continue;
        }
        DestroyTexture(texture->second);
        texture = mNativePicaTextures.erase(texture);
    }
    RetireGpuTextureCopySurfacesForTargetReset();
    // Capture and derived extents follow the internal scale: pooled images of
    // the old scale would never match again.
    ReleaseGpuCopyImagePool();
    for (auto& [key, target] : mNativePicaRenderTargets) {
        DestroyNativePicaRenderTarget(target);
    }
    mNativePicaRenderTargets.clear();
    mNativePicaDisplayDepthTargets.clear();
    // The scenes before UI belong to the targets' frames and are retired with
    // them, whatever happens to the presented pixels.
    DestroyNativePicaSceneBeforeUiImages();
    if (!preserveDisplayImages) {
        for (auto& [key, image] : mNativePicaDisplayImages) {
            DestroyNativePicaDisplayImage(image);
        }
        mNativePicaDisplayImages.clear();
        mLastPresentedNativePicaDisplayTransfer.reset();
    } else {
        // Retained pixels remain presentable, but their old scene/depth guides
        // no longer exist. The next native transfer republishes that capability;
        // until then the graph must not declare reads from retired attachments,
        // and a resolved-composition provenance from an earlier frame must not
        // suppress the scene stages for these pixels.
        for (auto& [key, image] : mNativePicaDisplayImages) {
            image.SceneResolved = false;
            image.SceneEffectsResolved = false;
        }
    }
    mInvalidatedNativePicaRenderTargetAddresses.clear();
    mActiveNativePicaRenderTarget = nullptr;
    mNativePicaRenderPassActive = false;
    mNativePicaPresentedThisFrame = false;
}

} // namespace Fast

#endif
