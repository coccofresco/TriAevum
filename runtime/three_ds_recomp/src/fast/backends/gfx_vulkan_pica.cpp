// Native PICA on Vulkan: shader and geometry resources, render passes, render targets, display images,
// memory fills and descriptor sets. Other topics: gfx_vulkan_pica_*.cpp.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/extension/pica_attachment_formats.h"
#include "gfx_vulkan_raster_stats.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <map>
#include <string>
#include "fast/extension/native_ui_overlay.h"
#include "fast/renderer3ds/title_shader_preparation.h"

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
        const std::array<VkFormat, Extension::kPicaColorAttachmentCount> colorFormats{
            VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            Extension::PicaGeometryGuideFormat()
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
            createImage(Extension::PicaGeometryGuideFormat(),
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
                createImage(Extension::PicaGeometryGuideFormat(), msaaColorUsage, mNativePicaSampleCount,
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
        view.format = Extension::PicaGeometryGuideFormat();
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
            view.format = Extension::PicaGeometryGuideFormat();
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
    publishSurface(inserted.OutlineGeometryGuideImage, Extension::PicaGeometryGuideFormat(),
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
    if (RasterStats::PerDraw() && target.NormalGuideView != VK_NULL_HANDLE)
        RasterStats::Collector::Instance().ResetDrawQueries(
            mDevice, mCommandBuffers[mCurrentFrame], mCurrentFrame, mFrameCounter);
    const bool rasterStatsQuery = RasterStats::Enabled() &&
        RasterStats::Collector::Instance().PrepareScope(
            mDevice, mCommandBuffers[mCurrentFrame], mCurrentFrame, mFrameCounter,
            target.NormalGuideView != VK_NULL_HANDLE, target.Width, target.Height);
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
    if (rasterStatsQuery)
        RasterStats::Collector::Instance().BeginQuery(mCommandBuffers[mCurrentFrame]);
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

void GfxRenderingAPIVulkan::EndNativePicaRenderPass(std::source_location where) {
    if (!mNativePicaRenderPassActive) {
        return;
    }
    // A deferred prepass window belongs to this scope: record it before it ends.
    FlushNativePicaPrepass();
    if (!mNativePicaRenderPassActive) return; // ended by the window's A/B check
    if (mGpuProfiler.TimelineEnabled()) {
        // Which callers end the native scope on an instrumented (guided)
        // target: each end costs a reload of its attachments later.
        static std::map<std::string, uint64_t> ends;
        static uint64_t frames = 0;
        static uint32_t lastFrame = UINT32_MAX;
        if (mActiveNativePicaRenderTarget != nullptr &&
            mActiveNativePicaRenderTarget->NormalGuideView != VK_NULL_HANDLE) {
            const char* file = where.file_name();
            if (const char* slash = std::strrchr(file, '/')) file = slash + 1;
            ++ends[std::string(file) + ":" + std::to_string(where.line())];
        }
        if (mFrameCounter != lastFrame) {
            lastFrame = static_cast<uint32_t>(mFrameCounter);
            if (++frames == 300U) {
                for (const auto& [site, count] : ends)
                    std::fprintf(stderr, "[pica-scope-end] %6.2f per frame  %s\n",
                                 static_cast<double>(count) / 300.0, site.c_str());
                ends.clear();
                frames = 0;
            }
        }
    }
    if (RasterStats::Enabled())
        RasterStats::Collector::Instance().EndQuery(mCommandBuffers[mCurrentFrame]);
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
    FlushNativePicaPrepass(); // the window precedes the fill

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
        if (RasterStats::PerDraw() && RasterStats::Collector::Instance().TraceFrame())
            std::fprintf(stderr, "[raster-fill] before_order=%u color=%u depth=%u target=%08x %ux%u guides=%u\n",
                         RasterStats::Collector::Instance().DrawOrder(mCurrentFrame), coversColor ? 1U : 0U,
                         coversDepth ? 1U : 0U, target.Key.ColorPhysicalAddress, target.Width, target.Height,
                         target.NormalGuideView != VK_NULL_HANDLE ? 1U : 0U);
        if (coversColor) {
            // Filled colour is new content: it carries no resolved scene
            // effects any more, so the provenance serial moves on.
            target.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
            target.GeometryGuideResetSerial =
                mActiveNativePicaAttachments.Binds(Extension::PicaColorAttachment::OutlineGeometryGuide)
                ? target.ContentWriteSerial : 0U;
        }
        fill = mPendingNativePicaMemoryFills.erase(fill);
    }
}

bool GfxRenderingAPIVulkan::NativePicaGeometryGuideAtReset(const NativePicaRenderTarget& target) const {
    if (target.GeometryGuideResetSerial != 0U && target.GeometryGuideResetSerial == target.ContentWriteSerial)
        return true;
    if (!ActivePicaAttachmentRequirements().Binds(Extension::PicaColorAttachment::OutlineGeometryGuide))
        return false;
    const uint64_t colorEnd = static_cast<uint64_t>(target.Key.ColorPhysicalAddress) +
                              static_cast<uint64_t>(target.Key.Width) * target.Key.Height *
                                  NativePicaColorBytesPerPixel(target.Key.ColorFormat);
    for (const auto& fill : mPendingNativePicaMemoryFills)
        if (fill.RenderTargetNamespace == target.Key.RenderTargetNamespace &&
            fill.StartPhysicalAddress <= target.Key.ColorPhysicalAddress && fill.EndPhysicalAddress >= colorEnd)
            return true;
    return false;
}

bool GfxRenderingAPIVulkan::NativePicaAbArmed() {
    auto& ab = mNativePicaAb;
    if (!ab.Parsed) {
        ab.Parsed = true;
        if (const char* frame = std::getenv("TRIAEVUM_AB_FRAME")) {
            ab.Frame = static_cast<uint32_t>(std::strtoul(frame, nullptr, 10));
            const char* name = std::getenv("TRIAEVUM_AB_SWITCH");
            const std::string_view selected = name != nullptr ? name : "atmosphere_geometry_write";
            if (selected == "atmosphere_geometry_write") {
                ab.Switches = Extension::kPicaAbAtmosphereGeometryWrite;
            } else if (selected == "probe_no_color_write") {
                ab.Switches = Extension::kPicaAbProbeNoColorWrite;
            } else if (selected == "geometry_guide_partial") {
                ab.Switches = Extension::kPicaAbGeometryGuidePartial;
            } else if (selected == "replace_blend") {
                ab.Switches = Extension::kPicaAbReplaceBlend;
            } else if (selected == "shadow_receiver_cull") {
                // A uniform switch: draws whose light mask the cull cleared.
                ab.Switches = Extension::kPicaAbShadowReceiverCull;
            } else if (selected == "depth_prepass") {
                // A whole prepass window (FlushNativePicaPrepass), not one draw.
                ab.Switches = 0U;
            } else {
                std::fprintf(stderr, "[ab] unknown TRIAEVUM_AB_SWITCH=%s; disarmed\n", name);
                ab.Frame = UINT32_MAX;
            }
            const char* control = std::getenv("TRIAEVUM_AB_CONTROL");
            ab.Control = control != nullptr && control[0] == '1';
            // TRIAEVUM_AB_SKIP_STEP=S: check k (0, 1, ...) passes over the
            // first k*S draws the switch changes, so repeats sample other draws.
            if (const char* step = std::getenv("TRIAEVUM_AB_SKIP_STEP"))
                ab.SkipStep = static_cast<uint32_t>(std::strtoul(step, nullptr, 10));
            std::fprintf(stderr, "[ab] armed frame=%u switch=%s control=%u\n", ab.Frame,
                         std::string(selected).c_str(), ab.Control ? 1U : 0U);
        }
    }
    if (ab.Pending && mFrameCounter >= ab.RecordedFrame + 4U) FinishNativePicaAb();
    return !ab.Done && ab.Frame != UINT32_MAX && mFrameCounter >= ab.Frame &&
        mNativePicaSampleCount == VK_SAMPLE_COUNT_1_BIT;
}

void GfxRenderingAPIVulkan::RunNativePicaAbDraw(NativePicaRenderTarget& target, const char* label,
                                               const std::function<void(bool variantB)>& record) {
    auto& ab = mNativePicaAb;
    if (!mNativePicaRenderPassActive || mActiveNativePicaRenderTarget != &target) {
        record(false);
        return;
    }
    ab.Done = true;
    const auto formats = Extension::BoundPicaColorAttachmentFormats(mActiveNativePicaAttachments);
    const std::array<VkImage, Extension::kPicaColorAttachmentCount> colors{
        target.ColorImage,         target.NormalGuideImage, target.MaterialGuideImage,
        target.RigidMotionGuideImage, target.AmbientGuideImage, target.FogGuideImage,
        target.OutlineGeometryGuideImage, target.DiffuseAlbedoGuideImage };
    static constexpr const char* kNames[] = { "colour", "normal", "material", "rigid_motion",
                                              "ambient", "fog", "geometry", "diffuse_albedo" };
    ab.Images.clear();
    ab.Width = target.Width;
    ab.Height = target.Height;
    for (size_t index = 0; index < colors.size(); ++index) {
        if (formats[index] == VK_FORMAT_UNDEFINED || colors[index] == VK_NULL_HANDLE) continue;
        NativePicaAbImage image;
        image.Name = kNames[index];
        image.Target = colors[index];
        image.Format = formats[index];
        image.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        switch (formats[index]) {
        case VK_FORMAT_R8G8B8A8_UNORM: image.TexelBytes = 4U; image.ComponentBytes = 1U; break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_UNORM: image.TexelBytes = 8U; image.ComponentBytes = 2U; break;
        case VK_FORMAT_R32G32B32A32_UINT: image.TexelBytes = 16U; image.ComponentBytes = 4U; break;
        case VK_FORMAT_R32G32_UINT: image.TexelBytes = 8U; image.ComponentBytes = 4U; break;
        default:
            std::fprintf(stderr, "[ab] %s: format %d not compared\n", image.Name, static_cast<int>(formats[index]));
            continue;
        }
        ab.Images.push_back(image);
    }
    {
        NativePicaAbImage depth;
        depth.Name = "depth";
        depth.Target = target.DepthImage;
        depth.Format = mDepthFormat;
        depth.Aspect = NativeDepthAspect(mDepthFormat);
        depth.TexelBytes = depth.ComponentBytes =
            (mDepthFormat == VK_FORMAT_D16_UNORM || mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT) ? 2U : 4U;
        ab.Images.push_back(depth);
    }
    const VkDeviceSize pixels = static_cast<VkDeviceSize>(target.Width) * target.Height;
    for (auto& image : ab.Images) {
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { target.Width, target.Height, 1 };
        info.mipLevels = info.arrayLayers = 1;
        info.format = image.Format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        CheckNativeVk(vkCreateImage(mDevice, &info, nullptr, &image.Snapshot), "vkCreateImage(A/B snapshot)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, image.Snapshot, &requirements);
        VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckNativeVk(vkAllocateMemory(mDevice, &allocation, nullptr, &image.SnapshotMemory), "vkAllocateMemory(A/B)");
        CheckNativeVk(vkBindImageMemory(mDevice, image.Snapshot, image.SnapshotMemory, 0), "vkBindImageMemory(A/B)");
        image.StencilOffset = pixels * image.TexelBytes;
        image.Bytes = image.StencilOffset + ((image.Aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U ? pixels : 0U);
        const int sides = image.Aspect == VK_IMAGE_ASPECT_COLOR_BIT ? 2 : 3;
        for (int side = 0; side < sides; ++side) {
            VkBufferCreateInfo buffer{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buffer.size = image.Bytes;
            buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            CheckNativeVk(vkCreateBuffer(mDevice, &buffer, nullptr, &image.Readback[side]), "vkCreateBuffer(A/B)");
            vkGetBufferMemoryRequirements(mDevice, image.Readback[side], &requirements);
            allocation.allocationSize = requirements.size;
            // Cached host memory: the comparison reads every byte (write-combined
            // memory made one check take ~50 s at 4K).
            allocation.memoryTypeIndex = FindReadbackMemoryType(requirements.memoryTypeBits);
            CheckNativeVk(vkAllocateMemory(mDevice, &allocation, nullptr, &image.ReadbackMemory[side]),
                          "vkAllocateMemory(A/B readback)");
            CheckNativeVk(vkBindBufferMemory(mDevice, image.Readback[side], image.ReadbackMemory[side], 0),
                          "vkBindBufferMemory(A/B readback)");
        }
    }
    const VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
    // The targets rest in attachment layouts between scopes.
    const auto transition = [&](bool toGeneral, bool snapshots, bool fromUndefined) {
        std::vector<VkImageMemoryBarrier> barriers;
        for (const auto& image : ab.Images) {
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange = { image.Aspect, 0, 1, 0, 1 };
            const VkImageLayout attachment = image.Aspect == VK_IMAGE_ASPECT_COLOR_BIT
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            if (snapshots) {
                barrier.image = image.Snapshot;
                barrier.oldLayout = fromUndefined ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } else {
                barrier.image = image.Target;
                barrier.oldLayout = toGeneral ? attachment : VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = toGeneral ? VK_IMAGE_LAYOUT_GENERAL : attachment;
            }
            barriers.push_back(barrier);
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    };
    const auto copyImages = [&](bool toSnapshot) {
        for (const auto& image : ab.Images) {
            VkImageCopy region{};
            region.srcSubresource = { image.Aspect, 0, 0, 1 };
            region.dstSubresource = region.srcSubresource;
            region.extent = { target.Width, target.Height, 1 };
            vkCmdCopyImage(command, toSnapshot ? image.Target : image.Snapshot, VK_IMAGE_LAYOUT_GENERAL,
                           toSnapshot ? image.Snapshot : image.Target, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        }
    };
    const auto readBack = [&](int side) {
        for (const auto& image : ab.Images) {
            VkBufferImageCopy region{};
            region.imageSubresource = { image.Aspect == VK_IMAGE_ASPECT_COLOR_BIT
                                            ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
            region.imageExtent = { target.Width, target.Height, 1 };
            vkCmdCopyImageToBuffer(command, image.Target, VK_IMAGE_LAYOUT_GENERAL, image.Readback[side], 1, &region);
            if ((image.Aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U) {
                region.bufferOffset = image.StencilOffset;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                vkCmdCopyImageToBuffer(command, image.Target, VK_IMAGE_LAYOUT_GENERAL, image.Readback[side], 1,
                                       &region);
            }
        }
    };
    EndNativePicaRenderPass();
    transition(true, false, false);
    transition(true, true, true);
    copyImages(true);
    // The depth before the window (classifies differing pixels: a tie with it
    // or one inside the window).
    transition(true, true, false);
    for (const auto& image : ab.Images) {
        if (image.Aspect == VK_IMAGE_ASPECT_COLOR_BIT) continue;
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
        region.imageExtent = { target.Width, target.Height, 1 };
        vkCmdCopyImageToBuffer(command, image.Snapshot, VK_IMAGE_LAYOUT_GENERAL, image.Readback[2], 1, &region);
    }
    transition(false, false, false);
    BeginNativePicaRenderPass(target);
    record(true);
    EndNativePicaRenderPass();
    transition(true, false, false);
    readBack(1);
    transition(true, true, false);
    copyImages(false);
    transition(false, false, false);
    BeginNativePicaRenderPass(target);
    record(false);
    EndNativePicaRenderPass();
    transition(true, false, false);
    readBack(0);
    transition(false, false, false);
    BeginNativePicaRenderPass(target);
    ab.Pending = true;
    ab.RecordedFrame = mFrameCounter;
    ab.Label = label;
}

void GfxRenderingAPIVulkan::FinishNativePicaAb() {
    auto& ab = mNativePicaAb;
    ab.Pending = false;
    vkDeviceWaitIdle(mDevice);
    std::fprintf(stderr, "[ab] frame=%u %ux%u %s variant_b=%s\n", ab.RecordedFrame, ab.Width, ab.Height,
                 ab.Label.c_str(), ab.Control ? "same pipeline (control)" : "switch on");
    const uint64_t pixels = static_cast<uint64_t>(ab.Width) * ab.Height;
    // Depth (always the last image) of A and before the window, mapped first:
    // a differing colour/guide pixel whose final depth equals the depth before
    // the window is a tie with the pre-window surface; otherwise the tie is
    // inside the window.
    auto& depthImage = ab.Images.back();
    void* depthMapped[2]{};
    vkMapMemory(mDevice, depthImage.ReadbackMemory[0], 0, VK_WHOLE_SIZE, 0, &depthMapped[0]);
    vkMapMemory(mDevice, depthImage.ReadbackMemory[2], 0, VK_WHOLE_SIZE, 0, &depthMapped[1]);
    {
        const std::array<VkMappedMemoryRange, 2> ranges{{
            {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, depthImage.ReadbackMemory[0], 0, VK_WHOLE_SIZE},
            {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, depthImage.ReadbackMemory[2], 0, VK_WHOLE_SIZE}}};
        vkInvalidateMappedMemoryRanges(mDevice, 2U, ranges.data());
    }
    const auto* depthFinal = static_cast<const uint8_t*>(depthMapped[0]);
    const auto* depthBefore = static_cast<const uint8_t*>(depthMapped[1]);
    const uint32_t depthBytes = depthImage.TexelBytes == 4U &&
            (depthImage.Format == VK_FORMAT_D24_UNORM_S8_UINT || depthImage.Format == VK_FORMAT_X8_D24_UNORM_PACK32)
        ? 3U : depthImage.TexelBytes;
    for (auto& image : ab.Images) {
        const bool isDepth = &image == &depthImage;
        void* mapped[2]{};
        if (isDepth) mapped[0] = depthMapped[0];
        else vkMapMemory(mDevice, image.ReadbackMemory[0], 0, VK_WHOLE_SIZE, 0, &mapped[0]);
        vkMapMemory(mDevice, image.ReadbackMemory[1], 0, VK_WHOLE_SIZE, 0, &mapped[1]);
        const std::array<VkMappedMemoryRange, 2> ranges{{
            {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, image.ReadbackMemory[0], 0, VK_WHOLE_SIZE},
            {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, image.ReadbackMemory[1], 0, VK_WHOLE_SIZE}}};
        vkInvalidateMappedMemoryRanges(mDevice, 2U, ranges.data());
        auto* a = static_cast<uint8_t*>(mapped[0]);
        auto* b = static_cast<uint8_t*>(mapped[1]);
        // D24 depth texels are copied as 32 bits with 8 undefined high bits.
        if (image.Format == VK_FORMAT_D24_UNORM_S8_UINT || image.Format == VK_FORMAT_X8_D24_UNORM_PACK32)
            for (uint64_t pixel = 0; pixel < pixels; ++pixel) a[pixel * 4U + 3U] = b[pixel * 4U + 3U] = 0U;
        const auto compare = [&](const char* name, const uint8_t* left, const uint8_t* right, uint32_t texel,
                                 uint32_t component) {
            uint64_t differing = 0, maxDiff = 0, nonZero = 0, preWindowTie = 0;
            std::string where;
            for (uint64_t pixel = 0; pixel < pixels; ++pixel) {
                const uint8_t* l = left + pixel * texel;
                const uint8_t* r = right + pixel * texel;
                for (uint32_t byte = 0; byte < texel; ++byte)
                    if (l[byte] != 0U) { ++nonZero; break; }
                if (std::memcmp(l, r, texel) == 0) continue;
                ++differing;
                const bool tie = std::memcmp(depthFinal + pixel * depthImage.TexelBytes,
                                             depthBefore + pixel * depthImage.TexelBytes, depthBytes) == 0;
                if (tie) ++preWindowTie;
                if (differing <= 8U) {
                    char item[64];
                    std::snprintf(item, sizeof(item), " (%llu,%llu)%s",
                                  static_cast<unsigned long long>(pixel % ab.Width),
                                  static_cast<unsigned long long>(pixel / ab.Width), tie ? "pre" : "in");
                    where += item;
                }
                for (uint32_t offset = 0; offset < texel; offset += component) {
                    uint64_t lv = 0, rv = 0;
                    std::memcpy(&lv, l + offset, component);
                    std::memcpy(&rv, r + offset, component);
                    maxDiff = std::max(maxDiff, lv > rv ? lv - rv : rv - lv);
                }
            }
            std::fprintf(stderr, "[ab]   %-14s differing_pixels=%llu max_abs_diff=%llu (raw units) nonzero_pixels_a=%llu\n",
                         name, static_cast<unsigned long long>(differing), static_cast<unsigned long long>(maxDiff),
                         static_cast<unsigned long long>(nonZero));
            if (differing != 0U)
                std::fprintf(stderr, "[ab]     %s: pre_window_depth_tie=%llu in_window=%llu at%s\n", name,
                             static_cast<unsigned long long>(preWindowTie),
                             static_cast<unsigned long long>(differing - preWindowTie), where.c_str());
            // RGBA8 targets: non-zero texels per channel of A, and B >= 254
            // (the material guide's water mark).
            if (texel == 4U && component == 1U) {
                std::array<uint64_t, 5> channels{};
                for (uint64_t pixel = 0; pixel < pixels; ++pixel) {
                    const uint8_t* l = left + pixel * 4U;
                    for (uint32_t c = 0; c < 4U; ++c) channels[c] += l[c] != 0U ? 1U : 0U;
                    channels[4] += l[2] >= 254U ? 1U : 0U;
                }
                std::fprintf(stderr, "[ab]     %s: nonzero_r=%llu g=%llu b=%llu a=%llu b_ge254=%llu\n", name,
                             static_cast<unsigned long long>(channels[0]), static_cast<unsigned long long>(channels[1]),
                             static_cast<unsigned long long>(channels[2]), static_cast<unsigned long long>(channels[3]),
                             static_cast<unsigned long long>(channels[4]));
            }
        };
        compare(image.Name, a, b, image.TexelBytes, image.ComponentBytes);
        if ((image.Aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0U)
            compare("stencil", a + image.StencilOffset, b + image.StencilOffset, 1U, 1U);
        if (!isDepth) vkUnmapMemory(mDevice, image.ReadbackMemory[0]);
        vkUnmapMemory(mDevice, image.ReadbackMemory[1]);
    }
    vkUnmapMemory(mDevice, depthImage.ReadbackMemory[0]);
    vkUnmapMemory(mDevice, depthImage.ReadbackMemory[2]);
    for (auto& image : ab.Images) {
        for (int side = 0; side < 3; ++side) {
            vkDestroyBuffer(mDevice, image.Readback[side], nullptr);
            vkFreeMemory(mDevice, image.ReadbackMemory[side], nullptr);
        }
        vkDestroyImage(mDevice, image.Snapshot, nullptr);
        vkFreeMemory(mDevice, image.SnapshotMemory, nullptr);
    }
    ab.Images.clear();
    std::fprintf(stderr, "[ab] done\n");
    // TRIAEVUM_AB_REPEAT=N: N checks, each armed again 7 frames after the last.
    static uint32_t remaining = [] {
        const char* value = std::getenv("TRIAEVUM_AB_REPEAT");
        return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 1U;
    }();
    if (remaining > 1U) {
        --remaining;
        ab.Done = false;
        ab.Frame = static_cast<uint32_t>(mFrameCounter) + 7U;
        ++ab.Checks;
        ab.SkipRemaining = ab.Checks * ab.SkipStep;
    }
}

void GfxRenderingAPIVulkan::SkyProbeDraw(NativePicaRenderTarget& target, const GfxNativePicaDrawView& draw,
                                         bool reactive, bool before) {
    auto& probe = mSkyProbe;
    if (!probe.Parsed) {
        probe.Parsed = true;
        const char* value = std::getenv("TRIAEVUM_SKY_PROBE");
        probe.Enabled = value != nullptr && value[0] == '1' && RasterStats::PerDraw();
    }
    if (!probe.Enabled || target.NormalGuideView == VK_NULL_HANDLE || target.MaterialGuideImage == VK_NULL_HANDLE ||
        mNativePicaSampleCount != VK_SAMPLE_COUNT_1_BIT)
        return;
    if (probe.Stage == 2) {
        if (mFrameCounter >= probe.RecordedFrame + 4U) FinishSkyProbe();
        return;
    }
    const bool atmosphere = draw.Composition.Layer == Renderer3ds::PicaCompositionLayer::Atmosphere;
    if (probe.Stage == 0) {
        if (before || !RasterStats::Collector::Instance().TraceFrame() || !atmosphere || !draw.DepthWriteEnabled)
            return;
        probe.Width = target.Width;
        probe.Height = target.Height;
        probe.RecordedFrame = mFrameCounter;
        probe.WindowDraws = 0;
        probe.AfterSkyGroup = false;
        probe.Log.clear();
        SkyProbeReadback(target, 0);
        probe.Stage = 1;
        return;
    }
    if (mFrameCounter != probe.RecordedFrame) {
        // The scope ended without a later non-opaque draw: nothing to compare.
        std::fprintf(stderr, "[sky-probe] frame=%llu window ended with the frame (draws=%u)%s\n",
                     static_cast<unsigned long long>(probe.RecordedFrame), probe.WindowDraws, probe.Log.c_str());
        probe.Stage = 0;
        return;
    }
    if (!before) return;
    const bool translucent = UsesTranslucentBlend(draw.Blend);
    const bool opaque = draw.DepthTestEnabled && draw.DepthWriteEnabled && !translucent;
    char line[256];
    std::snprintf(line, sizeof(line), "\n[sky-probe]   #%u pipeline=%llx layer=%u domain=%u dt=%u dw=%u cmp=%u blend=%u/%u "
                  "eq=%u translucent=%u reactive=%u mask=%x alpha_test=%u",
                  probe.WindowDraws, static_cast<unsigned long long>(draw.CanonicalPipelineId),
                  static_cast<unsigned>(draw.Composition.Layer), static_cast<unsigned>(draw.CompositionDomain),
                  draw.DepthTestEnabled ? 1U : 0U, draw.DepthWriteEnabled ? 1U : 0U,
                  static_cast<unsigned>(draw.DepthCompare), draw.Blend.Enabled ? 1U : 0U,
                  static_cast<unsigned>(draw.Blend.SourceRgb) * 16U + static_cast<unsigned>(draw.Blend.DestRgb),
                  static_cast<unsigned>(draw.Blend.EquationRgb), translucent ? 1U : 0U, reactive ? 1U : 0U,
                  draw.ColorWriteMask, draw.AlphaTestEnabled ? 1U : 0U);
    // The sky group: the Atmosphere draws that follow the sky directly.
    if (atmosphere && !probe.AfterSkyGroup) {
        if (probe.Log.size() < 64000U) probe.Log += line;
        ++probe.WindowDraws;
        return;
    }
    probe.AfterSkyGroup = true;
    if (opaque && !atmosphere) {
        if (probe.Log.size() < 64000U) probe.Log += line;
        ++probe.WindowDraws;
        return;
    }
    probe.Log += line;
    probe.Log += " <- first non-opaque draw: second readback before it";
    SkyProbeReadback(target, 1);
    probe.Stage = 2;
}

void GfxRenderingAPIVulkan::SkyProbeReadback(NativePicaRenderTarget& target, int side) {
    auto& probe = mSkyProbe;
    FlushNativePicaPrepass();
    const bool active = mNativePicaRenderPassActive && mActiveNativePicaRenderTarget == &target;
    if (!active) return;
    const auto formats = Extension::BoundPicaColorAttachmentFormats(mActiveNativePicaAttachments);
    const VkFormat materialFormat = formats[static_cast<size_t>(Extension::PicaColorAttachment::MaterialGuide)];
    probe.MaterialBytes = materialFormat == VK_FORMAT_R8G8B8A8_UNORM ? 4U : 0U;
    probe.DepthBytes = (mDepthFormat == VK_FORMAT_D16_UNORM || mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT) ? 2U : 4U;
    const VkDeviceSize pixels = static_cast<VkDeviceSize>(target.Width) * target.Height;
    const VkImageAspectFlags depthAspect = NativeDepthAspect(mDepthFormat);
    for (int kind = 0; kind < 2; ++kind) {
        const uint32_t bytes = kind == 0 ? probe.DepthBytes : probe.MaterialBytes;
        if (bytes == 0U || probe.Buffers[side][kind] != VK_NULL_HANDLE) continue;
        VkBufferCreateInfo buffer{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        buffer.size = pixels * bytes;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        CheckNativeVk(vkCreateBuffer(mDevice, &buffer, nullptr, &probe.Buffers[side][kind]), "vkCreateBuffer(sky probe)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(mDevice, probe.Buffers[side][kind], &requirements);
        VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindReadbackMemoryType(requirements.memoryTypeBits);
        CheckNativeVk(vkAllocateMemory(mDevice, &allocation, nullptr, &probe.Memory[side][kind]),
                      "vkAllocateMemory(sky probe)");
        CheckNativeVk(vkBindBufferMemory(mDevice, probe.Buffers[side][kind], probe.Memory[side][kind], 0),
                      "vkBindBufferMemory(sky probe)");
    }
    const VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
    EndNativePicaRenderPass();
    const auto transition = [&](bool toTransfer) {
        std::array<VkImageMemoryBarrier, 2> barriers{};
        uint32_t count = 0;
        for (int kind = 0; kind < 2; ++kind) {
            if ((kind == 0 ? probe.DepthBytes : probe.MaterialBytes) == 0U) continue;
            auto& barrier = barriers[count++];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = kind == 0 ? target.DepthImage : target.MaterialGuideImage;
            barrier.subresourceRange = { kind == 0 ? depthAspect : VkImageAspectFlags{ VK_IMAGE_ASPECT_COLOR_BIT },
                                         0, 1, 0, 1 };
            const VkImageLayout attachment = kind == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.oldLayout = toTransfer ? attachment : VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = toTransfer ? VK_IMAGE_LAYOUT_GENERAL : attachment;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, count, barriers.data());
    };
    transition(true);
    for (int kind = 0; kind < 2; ++kind) {
        if ((kind == 0 ? probe.DepthBytes : probe.MaterialBytes) == 0U) continue;
        VkBufferImageCopy region{};
        region.imageSubresource = { kind == 0 ? VkImageAspectFlags{ VK_IMAGE_ASPECT_DEPTH_BIT }
                                              : VkImageAspectFlags{ VK_IMAGE_ASPECT_COLOR_BIT }, 0, 0, 1 };
        region.imageExtent = { target.Width, target.Height, 1 };
        vkCmdCopyImageToBuffer(command, kind == 0 ? target.DepthImage : target.MaterialGuideImage,
                               VK_IMAGE_LAYOUT_GENERAL, probe.Buffers[side][kind], 1, &region);
    }
    transition(false);
    BeginNativePicaRenderPass(target);
}

void GfxRenderingAPIVulkan::FinishSkyProbe() {
    auto& probe = mSkyProbe;
    vkDeviceWaitIdle(mDevice);
    const uint64_t pixels = static_cast<uint64_t>(probe.Width) * probe.Height;
    const uint32_t depthMask = probe.DepthBytes == 2U ? 0xffffU
        : (mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT || mDepthFormat == VK_FORMAT_X8_D24_UNORM_PACK32) ? 0xffffffU
                                                                                                          : 0xffffffffU;
    void* mapped[2][2]{};
    for (int side = 0; side < 2; ++side)
        for (int kind = 0; kind < 2; ++kind)
            if (probe.Memory[side][kind] != VK_NULL_HANDLE)
                vkMapMemory(mDevice, probe.Memory[side][kind], 0, VK_WHOLE_SIZE, 0, &mapped[side][kind]);
    const auto depthAt = [&](int side, uint64_t pixel) {
        uint32_t value = 0;
        std::memcpy(&value, static_cast<const uint8_t*>(mapped[side][0]) + pixel * probe.DepthBytes, probe.DepthBytes);
        return value & depthMask;
    };
    // (a) the sky's depth: distinct values right after the sky.
    std::map<uint32_t, uint64_t> skyDepths;
    for (uint64_t pixel = 0; pixel < pixels && skyDepths.size() < 64U; ++pixel) ++skyDepths[depthAt(0, pixel)];
    uint32_t skyDepth = 0;
    uint64_t skyCount = 0;
    for (const auto& [value, count] : skyDepths)
        if (count > skyCount) { skyDepth = value; skyCount = count; }
    uint64_t covered = 0, nearer = 0, farther = 0, materialTie = 0, materialSkyNonZero = 0, materialLower = 0;
    uint32_t skyAlphaMin = 255U, skyAlphaMax = 0U;
    const bool material = mapped[0][1] != nullptr && mapped[1][1] != nullptr;
    for (uint64_t pixel = 0; pixel < pixels; ++pixel) {
        const uint32_t after = depthAt(1, pixel);
        const uint32_t sky = depthAt(0, pixel);
        uint32_t skyAlpha = 0, endAlpha = 0;
        if (material) {
            skyAlpha = static_cast<const uint8_t*>(mapped[0][1])[pixel * 4U + 3U];
            endAlpha = static_cast<const uint8_t*>(mapped[1][1])[pixel * 4U + 3U];
            skyAlphaMin = std::min(skyAlphaMin, skyAlpha);
            skyAlphaMax = std::max(skyAlphaMax, skyAlpha);
        }
        if (after == sky) continue;
        ++covered;
        (after > sky ? farther : nearer) += 1U; // "farther" = larger raw depth value
        if (material) {
            materialSkyNonZero += skyAlpha != 0U ? 1U : 0U;
            materialTie += (skyAlpha != 0U && endAlpha == skyAlpha) ? 1U : 0U;
            materialLower += endAlpha < skyAlpha ? 1U : 0U;
        }
    }
    std::fprintf(stderr,
                 "[sky-probe] frame=%llu %ux%u depth_bytes=%u sky_depth_distinct=%zu%s sky_depth=%08x (%llu px) "
                 "covered_at_second=%llu (f=%.4f) larger_raw=%llu smaller_raw=%llu material=%u sky_alpha=%u..%u "
                 "covered_with_sky_alpha>0=%llu covered_alpha_equal_sky>0=%llu covered_alpha_below_sky=%llu "
                 "window_draws=%u%s\n",
                 static_cast<unsigned long long>(probe.RecordedFrame), probe.Width, probe.Height, probe.DepthBytes,
                 skyDepths.size(), skyDepths.size() >= 64U ? "+" : "", skyDepth,
                 static_cast<unsigned long long>(skyCount), static_cast<unsigned long long>(covered),
                 pixels ? static_cast<double>(covered) / static_cast<double>(pixels) : 0.0,
                 static_cast<unsigned long long>(farther), static_cast<unsigned long long>(nearer), material ? 1U : 0U,
                 skyAlphaMin, skyAlphaMax, static_cast<unsigned long long>(materialSkyNonZero),
                 static_cast<unsigned long long>(materialTie), static_cast<unsigned long long>(materialLower),
                 probe.WindowDraws, probe.Log.c_str());
    for (int side = 0; side < 2; ++side)
        for (int kind = 0; kind < 2; ++kind) {
            if (mapped[side][kind] != nullptr) vkUnmapMemory(mDevice, probe.Memory[side][kind]);
            if (probe.Buffers[side][kind] != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, probe.Buffers[side][kind], nullptr);
            if (probe.Memory[side][kind] != VK_NULL_HANDLE) vkFreeMemory(mDevice, probe.Memory[side][kind], nullptr);
            probe.Buffers[side][kind] = VK_NULL_HANDLE;
            probe.Memory[side][kind] = VK_NULL_HANDLE;
        }
    probe.Log.clear();
    probe.Stage = 0;
}

void GfxRenderingAPIVulkan::ParseNativePicaPrepass() {
    auto& prepass = mNativePicaPrepass;
    if (prepass.Parsed) return;
    prepass.Parsed = true;
    // The title's default (TitleShaderPreparation::OpaqueDepthPrepass, from
    // its session) unless TRIAEVUM_DEPTH_PREPASS=0/1 says otherwise.
    const char* value = std::getenv("TRIAEVUM_DEPTH_PREPASS");
    prepass.Enabled = value != nullptr && value[0] != '\0'
        ? value[0] == '1'
        : Renderer3ds::InstalledTitleShaderPreparation().OpaqueDepthPrepass;
    const char* stats = std::getenv("TRIAEVUM_DEPTH_PREPASS_STATS");
    prepass.Stats = stats != nullptr && stats[0] == '1';
    const char* strict = std::getenv("TRIAEVUM_DEPTH_PREPASS_STRICT");
    prepass.Strict = strict == nullptr || strict[0] != '0';
    const char* discard = std::getenv("TRIAEVUM_DEPTH_PREPASS_DISCARD");
    prepass.Discard = discard == nullptr || discard[0] != '0';
    if (const char* trace = std::getenv("TRIAEVUM_DEPTH_PREPASS_TRACE_FRAME"))
        prepass.TraceFrame = std::strtoull(trace, nullptr, 10);
    const char* abSwitch = std::getenv("TRIAEVUM_AB_SWITCH");
    prepass.AbWindow = std::getenv("TRIAEVUM_AB_FRAME") != nullptr && abSwitch != nullptr &&
        std::string_view(abSwitch) == "depth_prepass";
    if (const char* minimum = std::getenv("TRIAEVUM_AB_MIN_WINDOW"))
        prepass.AbMinimumDraws = static_cast<uint32_t>(std::strtoul(minimum, nullptr, 10));
    std::fprintf(stderr, "[prepass] enabled=%u ab_window=%u\n", prepass.Enabled ? 1U : 0U,
                 prepass.AbWindow ? 1U : 0U);
}

void GfxRenderingAPIVulkan::PrintNativePicaPrepassStats() {
    auto& prepass = mNativePicaPrepass;
    if (!prepass.Trace.empty() && mFrameCounter != prepass.TraceFrame) {
        // E eligible, D eligible discarding, | window flush, b/n not
        // depth-writing opaque (test on/off), digits: ineligible reason.
        std::fprintf(stderr, "[prepass] trace frame=%llu %s\n", static_cast<unsigned long long>(prepass.TraceFrame),
                     prepass.Trace.c_str());
        prepass.Trace.clear();
    }
    if (prepass.Stats && prepass.Frames >= 600U) {
        const double frames = static_cast<double>(prepass.Frames);
        std::fprintf(stderr,
                     "[prepass] per frame: candidates %.1f windows %.2f window_draws %.1f max_window %llu "
                     "not_ready %.2f ineligible(no-depth-write %.1f less %.1f greater %.1f other-compare %.1f "
                     "stencil %.1f blend %.1f logic %.1f a2c/msaa %.1f not-nri %.1f coverage/mode %.1f "
                     "discard/fragdepth %.1f strict-cull-none %.1f)\n",
                     static_cast<double>(prepass.Candidates) / frames, static_cast<double>(prepass.Windows) / frames,
                     static_cast<double>(prepass.WindowDraws) / frames,
                     static_cast<unsigned long long>(prepass.MaxWindow), static_cast<double>(prepass.NotReady) / frames,
                     prepass.Ineligible[1] / frames, prepass.Ineligible[2] / frames, prepass.Ineligible[3] / frames,
                     prepass.Ineligible[4] / frames, prepass.Ineligible[5] / frames, prepass.Ineligible[6] / frames,
                     prepass.Ineligible[7] / frames, prepass.Ineligible[8] / frames, prepass.Ineligible[9] / frames,
                     prepass.Ineligible[10] / frames, prepass.Ineligible[11] / frames,
                     prepass.Ineligible[12] / frames);
        prepass.Frames = prepass.Windows = prepass.WindowDraws = prepass.Candidates = prepass.NotReady = 0;
        prepass.MaxWindow = 0;
        prepass.Ineligible = {};
    }
}

// Exactness of a window (all draws eligible: depth test and write with one
// non-strict compare, no stencil, blend, logic operation other than copy,
// discard, alpha test, gl_FragDepth or image writes; equal colour write masks):
// per pixel, the depth-only pass runs every fragment's depth test and write in
// the original order with the original compare, so it ends with the depth the
// sequential window ends with (D). With LEQUAL (GEQUAL symmetric) the depth
// only decreases, every fragment whose depth equals D passed its test in the
// sequential window, and the last fragment that passed there has depth D.
// The colour pass (EQUAL, no depth write) passes exactly the fragments at D,
// in order, so the last writer of every written channel is the same fragment
// with the same outputs (same programs, inputs and vertex state; the
// pipelines differ only in output-merger state). Draws outside the window are
// recorded before or after it, so they see the same attachments.
// A strict compare (LESS; GREATER symmetric) keeps the FIRST fragment at D,
// and only if D is nearer than the depth before the window (D0). The colour
// pass then runs the draws in reverse order, so the earliest draw holding a
// fragment at D writes last. That is exact unless (a) one draw has two
// fragments at the same pixel with bitwise equal depth D (the later one
// wins here) or (b) a fragment's depth equals D0 where no window fragment is
// nearer (it fails its LESS test there and passes EQUAL here). Strict draws
// without culling stay out ((a) is what two-sided surfaces drawn as coincident
// triangles do: EoT's alpha-tested foliage showed it, round 27). Neither case
// can be excluded by construction; TRIAEVUM_DEPTH_PREPASS_STRICT=0 leaves strict
// draws out, and the same-frame A/B (TRIAEVUM_AB_SWITCH=depth_prepass)
// measures them.
void GfxRenderingAPIVulkan::FlushNativePicaPrepass() {
    auto& prepass = mNativePicaPrepass;
    if (prepass.Draws.empty()) return;
    std::vector<NativePicaPrepassDraw> draws;
    draws.swap(prepass.Draws);
    NativePicaRenderTarget* target = prepass.Target;
    const bool strict = NativePicaPrepassStrict(prepass.Signature);
    prepass.Target = nullptr;
    prepass.Signature = 0;
    if (!mNativePicaRenderPassActive || mActiveNativePicaRenderTarget != target)
        throw std::runtime_error("depth prepass window outlived its rendering scope");
    // Developer probe 3 (changes the image): the depth-only pass with a 1x1
    // scissor (its vertex and primitive cost without fragments).
    static const bool probeScissor = [] {
        const char* value = std::getenv("TRIAEVUM_DEPTH_PREPASS_PROBE");
        return value != nullptr && value[0] == '3';
    }();
    const auto recordPass = [&](int pass) {
        // A strict compare's colour pass runs the draws in reverse order.
        const bool reverse = pass == 2 && strict;
        for (size_t index = 0; index < draws.size(); ++index) {
            auto& entry = draws[reverse ? draws.size() - 1U - index : index];
            if (entry.Desc.FrameIndex != mCurrentFrame)
                throw std::runtime_error("depth prepass window crossed a frame");
            auto desc = entry.Desc;
            desc.VertexBindings = entry.Bindings;
            desc.PipelineId = pass == 0 ? entry.Normal : pass == 1 ? entry.DepthOnly : entry.Equal;
            for (uint32_t part = 0; part < entry.ScissorCount; ++part) {
                desc.Scissor = entry.Scissors[part];
                if (pass == 1 && probeScissor) desc.Scissor.extent = { 1U, 1U };
                if (!mNriPicaPipelineBridge.BindOwnedDraw(desc))
                    throw std::runtime_error("NRI PICA draw binding failed in a depth prepass window");
            }
        }
    };
    const auto record = [&](bool withPrepass) {
        // Developer probe (changes the image): 1 = depth-only pass alone,
        // 2 = colour pass alone.
        static const int probe = [] {
            const char* value = std::getenv("TRIAEVUM_DEPTH_PREPASS_PROBE");
            return value != nullptr ? std::atoi(value) : 0;
        }();
        if (withPrepass) {
            if (probe != 2) recordPass(1);
            if (probe != 1) recordPass(2);
        } else {
            recordPass(0);
        }
    };
    if (prepass.TraceFrame == mFrameCounter) prepass.Trace += '|';
    ++prepass.Windows;
    prepass.WindowDraws += draws.size();
    prepass.MaxWindow = std::max<uint64_t>(prepass.MaxWindow, draws.size());
    if (prepass.AbWindow && draws.size() >= prepass.AbMinimumDraws && NativePicaAbArmed()) {
        char label[160];
        std::snprintf(label, sizeof(label), "depth_prepass window_draws=%zu scissor_parts=%u", draws.size(),
                      draws.front().ScissorCount);
        const bool control = NativePicaAbControl();
        // Variant B: the window with the prepass (or, for the control, without);
        // A, the real one: without the prepass.
        RunNativePicaAbDraw(*target, label, [&](bool variantB) { record(variantB && !control); });
    } else {
        record(prepass.Enabled);
    }
}

void GfxRenderingAPIVulkan::PaintNativePicaPictureBars(
    const Renderer3ds::PicaCanvasSurround& bars) {
    if (bars.Count == 0U) {
        return;
    }
    FlushNativePicaPrepass();
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
