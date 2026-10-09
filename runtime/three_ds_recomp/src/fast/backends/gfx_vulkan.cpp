// Vulkan backend: the Fast3D rendering API (shaders, textures, samplers, state, draws, framebuffers,
// ImGui). Other topics: gfx_vulkan_*.cpp; native PICA: gfx_vulkan_pica*.cpp.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"

namespace Fast {

const char* GfxRenderingAPIVulkan::GetName() {
    return "Vulkan";
}

int GfxRenderingAPIVulkan::GetMaxTextureSize() {
    if (mPhysicalDevice == VK_NULL_HANDLE) {
        return 4096;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    return static_cast<int>(properties.limits.maxImageDimension2D);
}

GfxClipParameters GfxRenderingAPIVulkan::GetClipParameters() {
    return { true, true };
}

void GfxRenderingAPIVulkan::UnloadShader(ShaderProgram*) {
    mCurrentShader = nullptr;
}

void GfxRenderingAPIVulkan::LoadShader(ShaderProgram* newPrg) {
    mCurrentShader = reinterpret_cast<VulkanShaderProgram*>(newPrg);
}

void GfxRenderingAPIVulkan::ClearShaderCache() {
    if (mDevice != VK_NULL_HANDLE) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(clear shader cache)");
        DestroyGraphicsPipelines();
        for (auto& [key, shader] : mShaders) {
            if (shader.VertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.VertexShader, nullptr);
            }
            if (shader.FragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.FragmentShader, nullptr);
            }
        }
    }
    mCurrentShader = nullptr;
    mShaders.clear();
}

ShaderProgram* GfxRenderingAPIVulkan::CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) {
    auto& shader = mShaders[{ shaderId0, shaderId1 }];
    shader = BuildShaderProgram(shaderId0, shaderId1);
    const std::string vertexSource = BuildVertexShaderSource(shader);
    const std::string fragmentSource = BuildFragmentShaderSource(shaderId0, shaderId1);
    const std::string sourceStem = "oot3d_" + std::to_string(shaderId0) + "_" +
                                   std::to_string(shaderId1);
    shader.VertexShader =
        CompileShaderModule(vertexSource, true, (sourceStem + ".vert").c_str());
    shader.FragmentShader =
        CompileShaderModule(fragmentSource, false, (sourceStem + ".frag").c_str());
    mCurrentShader = &shader;
    return reinterpret_cast<ShaderProgram*>(&shader);
}

ShaderProgram* GfxRenderingAPIVulkan::LookupShader(uint64_t shaderId0, uint64_t shaderId1) {
    const auto it = mShaders.find({ shaderId0, shaderId1 });
    return it == mShaders.end() ? nullptr : reinterpret_cast<ShaderProgram*>(&it->second);
}

void GfxRenderingAPIVulkan::ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    const auto* shader = reinterpret_cast<const VulkanShaderProgram*>(prg);
    *numInputs = shader != nullptr ? shader->NumInputs : 0;
    usedTextures[0] = shader != nullptr && shader->UsedTextures[0];
    usedTextures[1] = shader != nullptr && shader->UsedTextures[1];
}

uint32_t GfxRenderingAPIVulkan::NewTexture() {
    const uint32_t id = mNextTextureId++;
    mTextures.emplace(id, TextureRecord{});
    return id;
}

void GfxRenderingAPIVulkan::SelectTexture(int tile, uint32_t textureId) {
    if (tile < 0 || static_cast<size_t>(tile) >= mSelectedTextures.size()) {
        return;
    }
    mCurrentTextureUnit = static_cast<uint32_t>(tile);
    mSelectedTextures[mCurrentTextureUnit] = textureId;
}

void GfxRenderingAPIVulkan::UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) {
    if (rgba32Buf == nullptr || width == 0 || height == 0 ||
        mSelectedTextures[mCurrentTextureUnit] == 0) {
        return;
    }
    auto& texture = mTextures[mSelectedTextures[mCurrentTextureUnit]];
    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    texture.Rgba8.assign(rgba32Buf, rgba32Buf + byteCount);
    CreateTextureImage(texture, rgba32Buf, width, height);
}

void GfxRenderingAPIVulkan::SetSamplerParameters(int sampler, bool linearFilter, uint32_t cms,
                                                  uint32_t cmt) {
    if (sampler < 0 || static_cast<size_t>(sampler) >= mSelectedTextures.size()) {
        return;
    }
    const auto it = mTextures.find(mSelectedTextures[static_cast<size_t>(sampler)]);
    if (it == mTextures.end()) {
        return;
    }
    auto& state = it->second.SamplerState;
    state.MinFilter = linearFilter ? GfxNativeTextureFilter::Linear
                                   : GfxNativeTextureFilter::Nearest;
    state.MagFilter = state.MinFilter;
    state.WrapS = (cms & G_TX_CLAMP) != 0
                      ? GfxNativeTextureWrap::ClampToEdge
                      : (cms & G_TX_MIRROR) != 0 ? GfxNativeTextureWrap::MirroredRepeat
                                                 : GfxNativeTextureWrap::Repeat;
    state.WrapT = (cmt & G_TX_CLAMP) != 0
                      ? GfxNativeTextureWrap::ClampToEdge
                      : (cmt & G_TX_MIRROR) != 0 ? GfxNativeTextureWrap::MirroredRepeat
                                                 : GfxNativeTextureWrap::Repeat;
    if (!it->second.SamplerStateApplied ||
        !SameSamplerState(state, it->second.AppliedSamplerState)) {
        RecreateSampler(it->second);
    }
}

bool GfxRenderingAPIVulkan::UploadTextureMipLevel(uint32_t level, const uint8_t* rgba32Buf,
                                                  uint32_t width, uint32_t height) {
    if (level == 0 || rgba32Buf == nullptr || width == 0 || height == 0 ||
        mSelectedTextures[mCurrentTextureUnit] == 0) {
        return false;
    }
    auto it = mTextures.find(mSelectedTextures[mCurrentTextureUnit]);
    if (it == mTextures.end() || !it->second.Uploaded || level >= it->second.MipLevels ||
        level > it->second.UploadedMipLevels) {
        return false;
    }
    const uint32_t uploadedMipLevels = it->second.UploadedMipLevels;
    UploadTextureLevel(it->second, level, rgba32Buf, width, height);
    if (it->second.UploadedMipLevels != uploadedMipLevels) {
        RecreateSampler(it->second);
    }
    return true;
}

bool GfxRenderingAPIVulkan::SetNativeSamplerParameters(int sampler,
                                                       const GfxNativeSamplerState& state) {
    if (sampler < 0 || static_cast<size_t>(sampler) >= mSelectedTextures.size()) {
        return false;
    }
    const auto it = mTextures.find(mSelectedTextures[static_cast<size_t>(sampler)]);
    if (it == mTextures.end()) {
        return false;
    }
    it->second.SamplerState = state;
    if (!it->second.SamplerStateApplied ||
        !SameSamplerState(state, it->second.AppliedSamplerState)) {
        RecreateSampler(it->second);
    }
    return true;
}

void GfxRenderingAPIVulkan::SetDepthTestAndMask(bool depthTest, bool zUpd) {
    mCurrentDepthTest = depthTest;
    mCurrentDepthMask = zUpd;
}

void GfxRenderingAPIVulkan::SetZmodeDecal(bool decal) {
    mCurrentZmodeDecal = decal;
}

void GfxRenderingAPIVulkan::SetViewport(int x, int y, int width, int height) {
    mViewport = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                  static_cast<float>(height), 0.0f, 1.0f };
    mViewportSet = true;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::SetScissor(int x, int y, int width, int height) {
    mScissor.offset = { x, y };
    mScissor.extent = { static_cast<uint32_t>(std::max(0, width)),
                        static_cast<uint32_t>(std::max(0, height)) };
    mScissorSet = true;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::SetUseAlpha(bool useAlpha) {
    mBlendState = {};
    mBlendState.Enabled = useAlpha;
    if (useAlpha) {
        mBlendState.SourceRgb = GfxNativeBlendFactor::SourceAlpha;
        mBlendState.DestRgb = GfxNativeBlendFactor::OneMinusSourceAlpha;
        mBlendState.SourceAlpha = GfxNativeBlendFactor::SourceAlpha;
        mBlendState.DestAlpha = GfxNativeBlendFactor::OneMinusSourceAlpha;
    }
}

bool GfxRenderingAPIVulkan::SetNativeBlendState(const GfxNativeBlendState& state) {
    mBlendState = state;
    return true;
}

bool GfxRenderingAPIVulkan::SetNativeCullMode(GfxNativeCullMode mode) {
    mCullMode = mode;
    return true;
}

bool GfxRenderingAPIVulkan::SupportsOot3dPicaFogLut() const {
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dPicaFogShaderParameters(const uint32_t* lutWords,
                                                            size_t lutWordCount,
                                                            bool fogFlip,
                                                            uint64_t stateKey) {
    if (lutWords == nullptr || lutWordCount < mDrawUniforms.FogLut.size()) {
        return false;
    }
    if (!mFogStateValid || mFogStateKey != stateKey) {
        std::copy_n(lutWords, mDrawUniforms.FogLut.size(), mDrawUniforms.FogLut.begin());
        mFogStateKey = stateKey;
        mFogStateValid = true;
    }
    mDrawUniforms.FogFlip = fogFlip ? 1 : 0;
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dPicaAlphaTestShaderParameters(bool enabled,
                                                                 uint32_t function,
                                                                 uint8_t reference) {
    mDrawUniforms.AlphaTestEnabled = enabled ? 1 : 0;
    mDrawUniforms.AlphaTestFunction = static_cast<int32_t>(function);
    mDrawUniforms.AlphaTestReference = reference;
    return true;
}

bool GfxRenderingAPIVulkan::SupportsOot3dPicaTexture2() const {
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dNativeTransform(const float* rowMajorMatrix) {
    if (rowMajorMatrix == nullptr) {
        return false;
    }
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            mTransformPushConstants.ModelViewProjection[column * 4 + row] =
                rowMajorMatrix[row * 4 + column];
        }
    }
    return true;
}

bool GfxRenderingAPIVulkan::DrawTrianglesCached(uint64_t cacheId, uint64_t contentVersion, const float* bufVbo,
                                                 size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        cacheId == 0 || bufVbo == nullptr || bufVboLen == 0 || bufVboNumTris == 0) {
        return false;
    }

    auto cacheIt = mOot3dCachedVertexBuffers.find(cacheId);
    if (cacheIt == mOot3dCachedVertexBuffers.end()) {
        if (mOot3dCachedVertexBuffers.size() >= kOot3dCachedVertexBufferLimit) {
            const auto oldest = std::min_element(
                mOot3dCachedVertexBuffers.begin(), mOot3dCachedVertexBuffers.end(),
                [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrame < right.second.LastUsedFrame;
                });
            if (oldest != mOot3dCachedVertexBuffers.end()) {
                CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(evict cached vertex buffer)");
                for (auto& buffer : oldest->second.Buffers) {
                    DestroyBuffer(buffer);
                }
                mOot3dCachedVertexBuffers.erase(oldest);
            }
        }
        cacheIt = mOot3dCachedVertexBuffers.emplace(cacheId, CachedVertexBuffer{}).first;
    }

    auto& cached = cacheIt->second;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    auto& buffer = cached.Buffers[mCurrentFrame];
    if (buffer.Buffer == VK_NULL_HANDLE || buffer.Size < byteCount) {
        DestroyBuffer(buffer);
        buffer = CreateBuffer(byteCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
        cached.ContentVersions[mCurrentFrame] = 0;
    }
    if (cached.ContentVersions[mCurrentFrame] != contentVersion ||
        cached.FloatCount != bufVboLen || cached.TriangleCount != bufVboNumTris) {
        std::memcpy(buffer.Mapped, bufVbo, static_cast<size_t>(byteCount));
        cached.ContentVersions[mCurrentFrame] = contentVersion;
    }
    cached.FloatCount = bufVboLen;
    cached.TriangleCount = bufVboNumTris;
    cached.LastUsedFrame = mFrameCounter;
    DrawTrianglesFromBuffer(buffer.Buffer, 0, bufVboLen, bufVboNumTris);
    return true;
}

void GfxRenderingAPIVulkan::DrawTriangles(float bufVbo[], size_t bufVboLen,
                                           size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        bufVbo == nullptr || bufVboLen == 0 || bufVboNumTris == 0) {
        return;
    }
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    const VkDeviceSize alignedOffset = (frame.VertexBytesUsed + 15) & ~VkDeviceSize(15);
    if (alignedOffset + byteCount > frame.VertexBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan per-frame vertex arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + alignedOffset, bufVbo,
                static_cast<size_t>(byteCount));
    frame.VertexBytesUsed = alignedOffset + byteCount;

    DrawTrianglesFromBuffer(frame.VertexBuffer.Buffer, alignedOffset, bufVboLen,
                            bufVboNumTris);
}

void GfxRenderingAPIVulkan::DrawTrianglesFromBuffer(VkBuffer vertexBuffer,
                                                     VkDeviceSize vertexOffset,
                                                     size_t bufVboLen,
                                                     size_t bufVboNumTris) {
    if (!mFrameActive || mCurrentFramebuffer != 0 || mCurrentShader == nullptr ||
        vertexBuffer == VK_NULL_HANDLE || bufVboLen == 0 || bufVboNumTris == 0) {
        return;
    }
    const uint32_t drawCallIndex = mDrawCallCountThisFrame++;
    const size_t vertexCount = bufVboNumTris * 3;
    if (vertexCount == 0 || bufVboLen % vertexCount != 0 ||
        (bufVboLen / vertexCount) * sizeof(float) != mCurrentShader->VertexStride) {
        throw std::runtime_error(
            "OOT3D Vulkan packed vertex stride mismatch at draw " +
            std::to_string(drawCallIndex) + ": shader=" +
            std::to_string(mCurrentShader->ShaderId0) + "/" +
            std::to_string(mCurrentShader->ShaderId1) + ", packed=" +
            std::to_string(vertexCount == 0 ? 0 : bufVboLen / vertexCount) +
            " floats, pipeline=" +
            std::to_string(mCurrentShader->VertexStride / sizeof(float)) + " floats");
    }
    BeginRenderPassIfNeeded();
    if (!mRenderPassActive) {
        return;
    }

    auto& frame = mFrameResources[mCurrentFrame];
    VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
    const VkPipeline pipeline = GetOrCreatePipeline(*mCurrentShader);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    ApplyDynamicViewportAndScissor();
    vkCmdSetBlendConstants(commandBuffer, mBlendState.ConstantColor);

    const auto resolveTexture = [&](uint32_t unit) -> TextureRecord* {
        const auto textureIt = mTextures.find(mSelectedTextures[unit]);
        return textureIt != mTextures.end() && textureIt->second.Uploaded
                   ? &textureIt->second
                   : &mFallbackTexture;
    };
    std::array<TextureRecord*, 3> drawTextures = {
        mCurrentShader->UsedTextures[0] ? resolveTexture(0) : &mFallbackTexture,
        mCurrentShader->UsedTextures[1] ? resolveTexture(1) : &mFallbackTexture,
        mCurrentShader->UsesTexture2 ? resolveTexture(2) : &mFallbackTexture,
    };
    for (size_t unit = 0; unit < drawTextures.size(); ++unit) {
        mDrawUniforms.TextureWidth[unit] = static_cast<int32_t>(drawTextures[unit]->Width);
        mDrawUniforms.TextureHeight[unit] = static_cast<int32_t>(drawTextures[unit]->Height);
        mDrawUniforms.TextureFiltering[unit] = static_cast<int32_t>(mFilterMode);
    }
    const OffscreenFramebufferRecord* shadowFramebuffer = nullptr;
    if (IsOot3dShadow2dShader(mCurrentShader->NativeShaderId)) {
        const auto shadowIt =
            mOffscreenFramebuffers.find(mBoundOot3dShadow2dFramebufferId);
        if (shadowIt == mOffscreenFramebuffers.end() ||
            shadowIt->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
            shadowIt->second.ColorView == VK_NULL_HANDLE ||
            shadowIt->second.ColorSampler == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "OOT3D Vulkan Shadow2D shader has no bound R32UI shadow target");
        }
        shadowFramebuffer = &shadowIt->second;
        mDrawUniforms.TextureWidth[3] = static_cast<int32_t>(shadowFramebuffer->Width);
        mDrawUniforms.TextureHeight[3] = static_cast<int32_t>(shadowFramebuffer->Height);
    } else {
        mDrawUniforms.TextureWidth[3] = static_cast<int32_t>(mFallbackTexture.Width);
        mDrawUniforms.TextureHeight[3] = static_cast<int32_t>(mFallbackTexture.Height);
    }
    mDrawUniforms.TextureFiltering[3] = static_cast<int32_t>(FILTER_NONE);
    mDrawUniforms.FrameCount = mFrameCounter;
    mDrawUniforms.NoiseScale = mCurrentNoiseScale;

    EnsureFrameUniformCapacity(sizeof(mDrawUniforms), mStorageBufferAlignment);
    const VkDeviceSize uniformOffset =
        (frame.UniformBytesUsed + mStorageBufferAlignment - 1) &
        ~(mStorageBufferAlignment - 1);
    if (uniformOffset + sizeof(mDrawUniforms) > frame.UniformBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan per-frame uniform arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.UniformBuffer.Mapped) + uniformOffset,
                &mDrawUniforms, sizeof(mDrawUniforms));
    frame.UniformBytesUsed = uniformOffset + sizeof(mDrawUniforms);

    std::array<VkDescriptorImageInfo, 4> imageInfos{};
    for (uint32_t unit = 0; unit < 3; ++unit) {
        imageInfos[unit].sampler = drawTextures[unit]->Sampler;
        imageInfos[unit].imageView = drawTextures[unit]->View;
        imageInfos[unit].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    imageInfos[3].sampler = shadowFramebuffer != nullptr
                                ? shadowFramebuffer->ColorSampler
                                : mFallbackTexture.Sampler;
    imageInfos[3].imageView = shadowFramebuffer != nullptr
                                 ? shadowFramebuffer->ColorView
                                 : mFallbackTexture.View;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    TextureDescriptorKey descriptorKey;
    for (size_t unit = 0; unit < imageInfos.size(); ++unit) {
        descriptorKey.Samplers[unit] = imageInfos[unit].sampler;
        descriptorKey.ImageViews[unit] = imageInfos[unit].imageView;
    }
    auto descriptorIt = frame.TextureDescriptorSets.find(descriptorKey);
    if (descriptorIt == frame.TextureDescriptorSets.end()) {
        const VkDescriptorSet descriptorSet = AllocateTextureDescriptorSet();
        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        for (uint32_t unit = 0; unit < 4; ++unit) {
            descriptorWrites[unit].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[unit].dstSet = descriptorSet;
            descriptorWrites[unit].dstBinding = unit;
            descriptorWrites[unit].descriptorCount = 1;
            descriptorWrites[unit].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[unit].pImageInfo = &imageInfos[unit];
        }
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = frame.UniformBuffer.Buffer;
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(mDrawUniforms);
        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSet;
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        descriptorWrites[4].pBufferInfo = &uniformInfo;
        vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
        descriptorIt = frame.TextureDescriptorSets.emplace(descriptorKey, descriptorSet).first;
    }
    if (uniformOffset > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("OOT3D Vulkan dynamic uniform offset exceeds uint32_t");
    }
    const VkDescriptorSet descriptorSet = descriptorIt->second;
    const uint32_t dynamicUniformOffset = static_cast<uint32_t>(uniformOffset);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0,
                            1, &descriptorSet, 1, &dynamicUniformOffset);
    TransformPushConstants drawTransform = mTransformPushConstants;
    if (mTemporalJitterEnabled && mCurrentDepthTest != 0 &&
        !mOot3dShadow2dPassActive &&
        Oot3d::HasPerspectiveClipW(drawTransform.ModelViewProjection)) {
        drawTransform.ModelViewProjection =
            Oot3d::ApplyTemporalJitterToClipMatrix(
                drawTransform.ModelViewProjection, mTemporalJitterPixels,
                std::abs(mViewport.width), std::abs(mViewport.height));
    }
    vkCmdPushConstants(commandBuffer, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(drawTransform), &drawTransform);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(bufVboNumTris * 3), 1, 0, 0);
}

void GfxRenderingAPIVulkan::RegisterTitlePassPrograms(std::span<const Renderer::PassShaderArtifact> programs) {
    mNriInterop.Shaders().AddArtifacts(programs);
}

bool GfxRenderingAPIVulkan::UsesAlwaysAvailablePrograms() const {
    return mNativePicaPreparationMode == NativePicaPreparationMode::Hybrid ||
           mNativePicaPreparationMode == NativePicaPreparationMode::Uber;
}

void GfxRenderingAPIVulkan::PrepareNativePicaPrograms(
    std::span<const Renderer3ds::PicaVertexArtifact> vertices) {
    if (!mGraphicsPipelineLibrariesEnabled || !mNriPicaPipelineBridge.OwnedDrawsEnabled()) return;
    // Every shader-bearing part an always-available draw links: the title
    // vertex family, the superset effect programs (when draws use them) and
    // the canonical bases. Forge's device preparation creates the same parts.
    const Renderer3ds::PicaStartupProgramDevice device{mDepthFormat, mNativePicaSampleCount,
                                                       mPbrSeamsAvailable};
    using Item = Renderer3ds::PicaStartupProgramPart;
    std::vector<Item> items = Renderer3ds::PicaStartupProgramParts(
        vertices,
        UsesAlwaysAvailablePrograms() ? Oot3d::UberFragmentArtifacts()
                                      : std::span<const Renderer3ds::PicaFragmentArtifact>{},
        Renderer3ds::NativeFragmentArtifacts(), device);
    const auto samples = device.Samples;
    const auto prepare = [this, device](const Item& item) {
        return Renderer3ds::PreparePicaStartupProgramPart(mNriPicaPipelineBridge, item, device);
    };
    const auto start = std::chrono::steady_clock::now();
    std::fprintf(stderr, "TRIAEVUM_NATIVE_PROGRAM_PREPARATION_BEGIN parts=%zu background=%u\n",
                 items.size(), mPreparationQueue ? 1U : 0U);
    JoinStartupPreparation();
    mStartupPreparedParts = 0;
    mStartupParts = items.size();
    const auto finished = [start, count = items.size(), samples](size_t failures) {
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::fprintf(stderr, "TRIAEVUM_NATIVE_PROGRAM_PREPARATION_END parts=%zu failures=%zu samples=%u ns=%llu\n",
                     count, failures, static_cast<unsigned>(samples), static_cast<unsigned long long>(nanoseconds));
    };
    mNativePicaProgramsPrepared = true;
    if (!mPreparationQueue) {
        for (const auto& item : items) {
            if (!prepare(item)) throw std::runtime_error("native program preparation failed");
            ++mStartupPreparedParts;
        }
        finished(0);
        return;
    }
    // Background: draws never wait for preparation as a whole. A draw that
    // needs a part being created waits for that part alone; one not started
    // yet is created by the draw and found ready by its task. The parts use
    // the cores the frame leaves (below-normal priority), apart from the
    // specialized-program workers.
    struct Progress {
        std::atomic<size_t> Next{0};
        std::atomic<size_t> Remaining;
        std::atomic<size_t> Failures{0};
        std::vector<Item> Items;
    };
    auto progress = std::make_shared<Progress>();
    progress->Items = std::move(items);
    progress->Remaining = progress->Items.size();
    // About one thread per physical core, one left to the frame: driver
    // compilation oversubscribed across logical cores makes each part (and the
    // draw waiting for it) several times slower for no added throughput.
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2U, 2U, 9U) - 1U;
    for (unsigned thread = 0; thread < threads; ++thread) {
        mStartupPreparationThreads.emplace_back([this, prepare, progress, finished] {
            Renderer3ds::LowerPicaPreparationThreadPriority();
            for (size_t index; (index = progress->Next++) < progress->Items.size();) {
                if (!prepare(progress->Items[index])) ++progress->Failures;
                // A failed part is not prepared, but it is no longer pending.
                ++mStartupPreparedParts;
                if (--progress->Remaining == 0) finished(progress->Failures);
            }
        });
    }
}

Renderer3ds::PicaPreparationStatus GfxRenderingAPIVulkan::PreparationStatus() const {
    Renderer3ds::PicaPreparationStatus status;
    status.Parts = mStartupParts.load();
    status.PreparedParts = std::min(mStartupPreparedParts.load(), status.Parts);
    status.ProgramsReady = status.PreparedParts == status.Parts;
    status.UpscalerReady = !mDlssPreparationPending.load();
    return status;
}

bool GfxRenderingAPIVulkan::InitImGuiBackend() {
    if (mImGuiInitialized) return true;
    if (mDevice == VK_NULL_HANDLE || mOverlayRenderPass == VK_NULL_HANDLE ||
        mSwapchainImages.size() < 2) return false;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = mInstance;
    info.PhysicalDevice = mPhysicalDevice;
    info.Device = mDevice;
    info.QueueFamily = mGraphicsQueueFamily;
    info.Queue = mGraphicsQueue;
    info.RenderPass = mOverlayRenderPass;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<uint32_t>(mSwapchainImages.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineCache = mPipelineCache;
    info.DescriptorPoolSize = 128;
    mImGuiInitialized = ImGui_ImplVulkan_Init(&info);
    return mImGuiInitialized;
}

void GfxRenderingAPIVulkan::ShutdownImGuiBackend() {
    if (!mImGuiInitialized) return;
    ImGui_ImplVulkan_Shutdown();
    mImGuiInitialized = false;
}

void GfxRenderingAPIVulkan::NewImGuiFrame() {
    if (InitImGuiBackend()) ImGui_ImplVulkan_NewFrame();
}

void GfxRenderingAPIVulkan::RenderImGuiDrawData(ImDrawData* drawData) {
    if (!mImGuiInitialized || drawData == nullptr || !mFrameActive) return;
    if (!mRenderPassActive) {
        std::string error;
        if (!PrepareOverlay(&error)) return;
        // Presentation-only host frames can precede the first guest display
        // transfer at 60 Hz or uncapped rates. In that case the overlay
        // preparation has no scene to rescan, so establish the ordinary
        // swapchain pass before ImGui emits indexed draws.
        if (!mRenderPassActive) {
            BeginRenderPassIfNeeded();
        }
    }
    if (!mRenderPassActive) return;
    ImGui_ImplVulkan_RenderDrawData(drawData, mCommandBuffers[mCurrentFrame]);
}

void GfxRenderingAPIVulkan::OnResize() {
    mSwapchainDirty = true;
}

bool GfxRenderingAPIVulkan::PublishSceneView(
    const Oot3d::TitleSceneViewSubmission& view) {
    if (view.WorldIdentity != 0U) {
        if (mTitleWorldIdentity != 0U && mTitleWorldIdentity != view.WorldIdentity)
            mTitleWorldChanged = true;
        mTitleWorldIdentity = view.WorldIdentity;
    }
    if (view.CameraAvailable) {
        Oot3d::SceneViewRuntime::Instance().PublishPerspectiveCamera(
            view.GuestFunction, view.GuestReturnAddress,
            view.Left, view.Right, view.Bottom, view.Top,
            view.NearPlane, view.FarPlane, view.Eye, view.At);
    } else {
        Oot3d::SceneViewRuntime::Instance().PublishPerspective(
            view.GuestFunction, view.GuestReturnAddress,
            view.Left, view.Right, view.Bottom, view.Top,
            view.NearPlane, view.FarPlane);
    }
    return true;
}

bool GfxRenderingAPIVulkan::PublishPicaFrameTemporalSample(
    const ::Fast::Renderer3ds::PicaFrameTemporalSample& sample) {
    return mPicaSceneFrame.SetTemporalSample(sample);
}

int GfxRenderingAPIVulkan::CreateFramebuffer() {
    const int id = mFramebufferCount++;
    mOffscreenFramebuffers.emplace(id, OffscreenFramebufferRecord{});
    return id;
}

void GfxRenderingAPIVulkan::UpdateFramebufferParameters(int fbId, uint32_t width, uint32_t height, uint32_t,
                                                        bool, bool, bool, bool) {
    if (fbId != 0) {
        return;
    }
    if (width == mRequestedWidth && height == mRequestedHeight) {
        return;
    }
    mRequestedWidth = width;
    mRequestedHeight = height;
    if (mInitialized) {
        // A fullscreen/mobile surface can grant a different extent from the
        // logical render request. Recreating it cannot change that constraint.
        // Real window changes still invalidate through OnResize/OUT_OF_DATE.
        const auto extent = ChooseExtent(QuerySwapchainSupport(mPhysicalDevice).Capabilities);
        if (extent.width != mSwapchainExtent.width || extent.height != mSwapchainExtent.height) {
            mSwapchainDirty = true;
        }
    }
}

bool GfxRenderingAPIVulkan::UpdateFramebufferParametersWithColorFormat(
    int fbId, uint32_t width, uint32_t height, uint32_t msaaLevel, bool openglInvertY,
    bool renderTarget, bool hasDepthBuffer, bool canExtractDepth,
    GfxFramebufferColorFormat colorFormat) {
    if (colorFormat == GfxFramebufferColorFormat::Rgba8) {
        if (fbId != 0) {
            return false;
        }
        UpdateFramebufferParameters(fbId, width, height, msaaLevel, openglInvertY,
                                    renderTarget, hasDepthBuffer, canExtractDepth);
        return true;
    }
    if (colorFormat != GfxFramebufferColorFormat::R32ui || fbId <= 0 || !renderTarget ||
        msaaLevel > 1 || !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end()) {
        return false;
    }
    width = std::max(width, 1U);
    height = std::max(height, 1U);
    auto& framebuffer = it->second;
    if (framebuffer.Framebuffer != VK_NULL_HANDLE && framebuffer.Width == width &&
        framebuffer.Height == height && framebuffer.HasDepthBuffer == hasDepthBuffer &&
        framebuffer.ColorFormat == colorFormat) {
        return true;
    }
    if (mFrameSubmitted) {
        CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                                std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences(recreate OOT3D Shadow2D framebuffer)");
    } else {
        CheckVk(vkDeviceWaitIdle(mDevice),
                "vkDeviceWaitIdle(recreate OOT3D Shadow2D framebuffer)");
    }
    DestroyOffscreenFramebuffer(framebuffer);
    framebuffer.ColorFormat = colorFormat;
    return CreateOot3dShadow2dFramebuffer(framebuffer, width, height, hasDepthBuffer);
}

bool GfxRenderingAPIVulkan::SupportsOot3dShadow2dR32uiPipeline() const {
    if (mPhysicalDevice == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, VK_FORMAT_R32_UINT, &properties);
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

bool GfxRenderingAPIVulkan::BindOot3dShadow2dTexture(int fbId, uint32_t textureUnit) {
    constexpr uint32_t kOot3dShadow2dTextureUnit = 6;
    if (textureUnit != kOot3dShadow2dTextureUnit) {
        return false;
    }
    const auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end() ||
        it->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
        it->second.ColorView == VK_NULL_HANDLE || it->second.ColorSampler == VK_NULL_HANDLE) {
        return false;
    }
    mBoundOot3dShadow2dFramebufferId = fbId;
    return true;
}

bool GfxRenderingAPIVulkan::SetOot3dShadow2dShaderParameters(uint32_t textureBias,
                                                             bool orthographic,
                                                             bool invert) {
    if (mCurrentShader == nullptr ||
        !IsOot3dShadow2dShader(mCurrentShader->NativeShaderId)) {
        return false;
    }
    mDrawUniforms.ShadowTextureBias = static_cast<int32_t>(textureBias);
    mDrawUniforms.ShadowOrthographic = orthographic ? 1 : 0;
    mDrawUniforms.ShadowInvert = invert ? 1 : 0;
    return true;
}

bool GfxRenderingAPIVulkan::StartOot3dShadow2dDepthEncodePass(int fbId,
                                                              uint32_t clearValue) {
    if (!mFrameActive || mOot3dShadow2dPassActive ||
        !SupportsOot3dShadow2dR32uiPipeline()) {
        return false;
    }
    const auto it = mOffscreenFramebuffers.find(fbId);
    if (it == mOffscreenFramebuffers.end() || it->second.Framebuffer == VK_NULL_HANDLE ||
        it->second.ColorFormat != GfxFramebufferColorFormat::R32ui ||
        !it->second.HasDepthBuffer) {
        return false;
    }
    if (mRenderPassActive) {
        if (mDrawCallCountThisFrame != 0) {
            return false;
        }
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    CreateOot3dShadow2dDepthEncodePipeline();
    if (mOot3dShadow2dDepthEncodePipeline == VK_NULL_HANDLE) {
        return false;
    }

    mViewportBeforeOot3dShadow2d = mViewport;
    mScissorBeforeOot3dShadow2d = mScissor;
    mViewportSetBeforeOot3dShadow2d = mViewportSet;
    mScissorSetBeforeOot3dShadow2d = mScissorSet;
    mFramebufferBeforeOot3dShadow2d = mCurrentFramebuffer;
    mCurrentFramebuffer = fbId;
    mActiveOot3dShadow2dFramebufferId = fbId;

    VkClearValue clearValues[2]{};
    clearValues[0].color.uint32[0] = clearValue;
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    beginInfo.renderPass = mOot3dShadow2dRenderPass;
    beginInfo.framebuffer = it->second.Framebuffer;
    beginInfo.renderArea.extent = { it->second.Width, it->second.Height };
    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &beginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    mOot3dShadow2dPassActive = true;
    mViewport = { 0.0f, 0.0f, static_cast<float>(it->second.Width),
                  static_cast<float>(it->second.Height), 0.0f, 1.0f };
    mScissor = { { 0, 0 }, { it->second.Width, it->second.Height } };
    mViewportSet = true;
    mScissorSet = true;
    vkCmdSetViewport(mCommandBuffers[mCurrentFrame], 0, 1, &mViewport);
    vkCmdSetScissor(mCommandBuffers[mCurrentFrame], 0, 1, &mScissor);
    return true;
}

void GfxRenderingAPIVulkan::EndOot3dShadow2dDepthEncodePass() {
    if (!mFrameActive || !mOot3dShadow2dPassActive) {
        return;
    }
    vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
    mOot3dShadow2dPassActive = false;
    mActiveOot3dShadow2dFramebufferId = -1;
    mCurrentFramebuffer = mFramebufferBeforeOot3dShadow2d;
    mViewport = mViewportBeforeOot3dShadow2d;
    mScissor = mScissorBeforeOot3dShadow2d;
    mViewportSet = mViewportSetBeforeOot3dShadow2d;
    mScissorSet = mScissorSetBeforeOot3dShadow2d;
}

bool GfxRenderingAPIVulkan::DrawOot3dShadow2dDepthEncodedTriangles(
    float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    if (!mFrameActive || !mOot3dShadow2dPassActive || bufVbo == nullptr ||
        bufVboNumTris == 0 || bufVboLen != bufVboNumTris * 3 * 4 ||
        mOot3dShadow2dDepthEncodePipeline == VK_NULL_HANDLE) {
        return false;
    }
    auto& frame = mFrameResources[mCurrentFrame];
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(bufVboLen * sizeof(float));
    const VkDeviceSize alignedOffset = (frame.VertexBytesUsed + 15) & ~VkDeviceSize(15);
    if (alignedOffset + byteCount > frame.VertexBuffer.Size) {
        throw std::runtime_error("OOT3D Vulkan Shadow2D vertex arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + alignedOffset, bufVbo,
                static_cast<size_t>(byteCount));
    frame.VertexBytesUsed = alignedOffset + byteCount;

    VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      mOot3dShadow2dDepthEncodePipeline);
    const VkBuffer vertexBuffer = frame.VertexBuffer.Buffer;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &alignedOffset);
    vkCmdSetViewport(commandBuffer, 0, 1, &mViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &mScissor);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(bufVboNumTris * 3), 1, 0, 0);
    return true;
}

void GfxRenderingAPIVulkan::StartDrawToFramebuffer(int fbId, float noiseScale) {
    mCurrentFramebuffer = fbId;
    mCurrentNoiseScale = noiseScale;
}

void GfxRenderingAPIVulkan::CopyFramebuffer(int, int, int, int, int, int, int, int, int, int) {
}

void GfxRenderingAPIVulkan::ClearFramebuffer(bool, bool) {
    if (mCurrentFramebuffer == 0) {
        BeginRenderPassIfNeeded();
    }
}

std::vector<uint8_t> GfxRenderingAPIVulkan::ReadSwapchainRgba8(uint32_t& copyWidthOut, uint32_t& copyHeightOut,
                                                               bool& bgraOut) {
    if (mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mDevice == VK_NULL_HANDLE ||
        (mSwapchain == VK_NULL_HANDLE && !mNriSwapchain.Active()) ||
        mCurrentFramebuffer != 0) {
        return {};
    }
    if (mFrameActive) {
        EndFrame();
    }
    if (!mFrameSubmitted) {
        return {};
    }
    CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                            std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences(readback)");

    const uint32_t copyWidth = mSwapchainExtent.width;
    const uint32_t copyHeight = mSwapchainExtent.height;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(copyWidth) * copyHeight * 4;
    auto readback = CreateBuffer(byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 true);
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = mSwapchainImages[mCurrentImage];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { copyWidth, copyHeight, 1 };
    vkCmdCopyImageToBuffer(commandBuffer, mSwapchainImages[mCurrentImage],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Buffer, 1, &copy);
    VkImageMemoryBarrier toPresent{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = mSwapchainImages[mCurrentImage];
    toPresent.subresourceRange = toTransfer.subresourceRange;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toPresent);
    EndImmediateCommands(commandBuffer);

    const auto* rgba8 = static_cast<const uint8_t*>(readback.Mapped);
    std::vector<uint8_t> pixels(rgba8, rgba8 + byteCount);
    DestroyBuffer(readback);
    copyWidthOut = copyWidth;
    copyHeightOut = copyHeight;
    bgraOut = mSwapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
              mSwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    return pixels;
}

void GfxRenderingAPIVulkan::ReadFramebufferToCPU(int, uint32_t width, uint32_t height, uint16_t* rgba16Buf) {
    if (rgba16Buf == nullptr || width == 0 || height == 0)
        return;
    uint32_t copyWidth = 0, copyHeight = 0;
    bool bgra = false;
    const auto pixels = ReadSwapchainRgba8(copyWidth, copyHeight, bgra);
    if (pixels.empty())
        return;
    if (!Renderer::CopyScaledFramebufferRgba5551(pixels, copyWidth, copyHeight, bgra,
                                                 {rgba16Buf, static_cast<size_t>(width) * height}, width, height))
        throw std::runtime_error("invalid framebuffer readback extent");
}

bool GfxRenderingAPIVulkan::ReadFramebufferRgb8ToCPU(uint32_t width, uint32_t height, uint8_t* rgb) {
    if (rgb == nullptr || width == 0 || height == 0)
        return false;
    uint32_t copyWidth = 0, copyHeight = 0;
    bool bgra = false;
    const auto pixels = ReadSwapchainRgba8(copyWidth, copyHeight, bgra);
    return !pixels.empty() &&
           Renderer::CopyScaledFramebufferRgb8(pixels, copyWidth, copyHeight, bgra,
                                               {rgb, static_cast<size_t>(width) * height * 3u}, width, height);
}

void GfxRenderingAPIVulkan::ResolveMSAAColorBuffer(int, int) {
}

std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GfxRenderingAPIVulkan::GetPixelDepth(int, const std::set<std::pair<float, float>>& coordinates) {
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> result;
    for (const auto& coordinate : coordinates) {
        result.emplace(coordinate, UINT16_MAX);
    }
    return result;
}

void* GfxRenderingAPIVulkan::GetFramebufferTextureId(int) {
    return nullptr;
}

void GfxRenderingAPIVulkan::SelectTextureFb(int) {
}

void GfxRenderingAPIVulkan::DeleteTexture(uint32_t texId) {
    const auto it = mTextures.find(texId);
    if (it == mTextures.end()) {
        return;
    }
    if (mDevice != VK_NULL_HANDLE) {
        CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(delete texture)");
        DestroyTexture(it->second);
    }
    mTextures.erase(it);
}

void GfxRenderingAPIVulkan::SetTextureFilter(FilteringMode mode) {
    mFilterMode = mode;
}

FilteringMode GfxRenderingAPIVulkan::GetTextureFilter() {
    return mFilterMode;
}

void GfxRenderingAPIVulkan::SetSrgbMode() {
    mSrgbMode = true;
}

ImTextureID GfxRenderingAPIVulkan::GetTextureById(int id) {
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(id));
}

void GfxRenderingAPIVulkan::SetCurrentPrimDepth(float depth) {
    mCurrentPrimDepth = depth;
    mDrawUniforms.PrimDepth = depth;
    mPrimDepthDirty = true;
}

} // namespace Fast

#endif
