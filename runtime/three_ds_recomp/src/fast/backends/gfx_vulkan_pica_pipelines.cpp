// Native PICA on Vulkan: shader modules, programs, pipelines and prewarm.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"

namespace Fast {

void GfxRenderingAPIVulkan::EnsureNativePicaVulkanShaderModules(
    const GfxNativePicaDrawView& draw, NativePicaShaderProgram& shader) {
    if (shader.VertexShader != VK_NULL_HANDLE && shader.FragmentShader != VK_NULL_HANDLE)
        return;
    if (shader.VertexShader != VK_NULL_HANDLE || shader.FragmentShader != VK_NULL_HANDLE)
        throw std::logic_error("partial native PICA Vulkan shader ownership");
    // NRI consumes SPIR-V directly. The combined-sampler program and Vulkan
    // modules belong exclusively to a draw that actually needs the fallback.
    const auto fragment = ResolveNativePicaShaderSpirv(draw.FragmentShaderSource,
        Oot3d::PicaAotShaderStage::Fragment, false, "native_pica_fallback.frag");
    const auto vertex = CreateShaderModuleFromSpirv(shader.NriVertexSpirv);
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    try {
        fragmentModule = CreateShaderModuleFromSpirv(fragment);
    } catch (...) {
        vkDestroyShaderModule(mDevice, vertex, nullptr);
        throw;
    }
    shader.VertexShader = vertex;
    shader.FragmentShader = fragmentModule;
    ++mNativePicaVulkanShaderPairCreations;
}

void GfxRenderingAPIVulkan::PrepareNativePicaProgram(NativePicaProgramPreparation& preparation) {
    // Owned inputs only; the render thread adopts the results.
    try {
        if (preparation.ResolveVertex)
            preparation.VertexSpirv = ResolveNativePicaShaderSpirv(
                preparation.VertexSource, Oot3d::PicaAotShaderStage::Vertex, true,
                (preparation.Stem + ".vert").c_str());
    } catch (const std::exception& exception) {
        preparation.CompileError = exception.what();
        preparation.ResolveVertex = false;
        preparation.Nri = false;
        preparation.VertexSource = "!";
        return;
    }
    if (!preparation.Nri) return;
    const auto nriVariant = Oot3d::BuildPicaNriFragmentShaderVariant(preparation.FragmentSource);
    preparation.NriApplied = nriVariant.Applied;
    if (!nriVariant.Applied) {
        preparation.NriError = nriVariant.Error;
        return;
    }
    preparation.NriFragmentSource = nriVariant.Source;
    try {
        preparation.NriFragmentSpirv = ResolveNativePicaShaderSpirv(
            nriVariant.Source, Oot3d::PicaAotShaderStage::NriFragment, false,
            (preparation.Stem + "_nri.frag").c_str());
    } catch (const std::exception& exception) {
        preparation.CompileError = exception.what();
    }
}

bool GfxRenderingAPIVulkan::AdoptNativePicaProgram(NativePicaShaderProgram& shader, bool complete) {
    const auto preparation = shader.Preparation;
    if (preparation == nullptr) return true;
    if (preparation->Task != nullptr && !preparation->Task->Done()) {
        if (!complete) return false;
        const auto start = std::chrono::steady_clock::now();
        mPreparationQueue->Complete(*preparation->Task);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        mNativePicaPreparationCounters.CompleteMilliseconds += milliseconds;
        mNativePicaPreparationCounters.MaximumCompleteMilliseconds =
            std::max(mNativePicaPreparationCounters.MaximumCompleteMilliseconds, milliseconds);
    }
    shader.Preparation.reset();
    // A vertex program that cannot be compiled has no draw (as before).
    if (preparation->VertexSource == "!") throw std::runtime_error(preparation->CompileError);
    if (preparation->ResolveVertex) shader.NriVertexSpirv = std::move(preparation->VertexSpirv);
    if (!preparation->Nri) return true;
    if (!preparation->NriApplied) {
        SPDLOG_WARN("NRI PICA shader contract unavailable for {}: {}",
                    preparation->Stem, preparation->NriError);
        return true;
    }
    shader.NriFragmentSource = Oot3d::IdentifyPicaAotShaderSource(preparation->NriFragmentSource);
    mPicaEffectiveShaderInventory.Observe(
        Oot3d::PicaAotShaderStage::NriFragment, preparation->NriFragmentSource,
        preparation->DescriptorSchemaVersion, preparation->CanonicalPipelineId,
        preparation->FragmentShaderKey, preparation->SettingsRevision);
    if (!preparation->CompileError.empty()) {
        if (mPicaAotShaderStrict) throw std::runtime_error(preparation->CompileError);
        SPDLOG_WARN("NRI PICA shader contract compile failed for {}: {}",
                    preparation->Stem, preparation->CompileError);
        return true;
    }
    shader.NriFragmentSpirv = std::move(preparation->NriFragmentSpirv);
    shader.NriDescriptorContract = true;
    return true;
}

Renderer3ds::PicaDevicePipelineRecord& GfxRenderingAPIVulkan::GetOrCreateNativePicaPipeline(
    const GfxNativePicaDrawView& draw,
    NativePicaShaderProgram& shader, bool writesReactiveMask,
    Oot3d::PicaShaderDomain domain,
    Oot3d::PicaShaderInstrumentationFeature requestedFeatures,
    Oot3d::PicaShaderInstrumentationFeature appliedFeatures,
    bool recordInventory, bool outlineOcclusionOnly, bool requireVulkan,
    NativePicaPipelineAcquisition acquisition, bool* pending) {
    if (pending != nullptr) *pending = false;
    if (recordInventory && mPicaPipelineInventory.Enabled()) {
        auto entry = Oot3d::DescribePicaGraphicsPipelineDraw(draw);
        entry.DescriptorSchemaVersion =
            draw.CanonicalDescriptorSchemaVersion;
        entry.Domain = domain == Oot3d::PicaShaderDomain::Canonical
            ? Oot3d::PicaGraphicsPipelineDomain::Canonical
            : Oot3d::PicaGraphicsPipelineDomain::Instrumented;
        entry.VertexShaderKey = draw.VertexShaderKey;
        entry.FragmentShaderKey = draw.FragmentShaderKey;
        entry.VertexSource = shader.VertexSource;
        entry.FragmentSource = shader.FragmentSource;
        entry.NriFragmentSource = shader.NriFragmentSource;
        entry.NriFragmentAvailable = shader.NriDescriptorContract;
        entry.RequestedFeatures = requestedFeatures;
        entry.AppliedFeatures = appliedFeatures;
        entry.AttachmentRequirementsKey =
            ActivePicaAttachmentRequirements().Key();
        entry.SampleCount =
            static_cast<uint8_t>(mNativePicaSampleCount);
        entry.WritesReactiveMask = writesReactiveMask;
        entry.OutlineOcclusionOnly = outlineOcclusionOnly;
        entry.ShaderOutputs = shader.FragmentOutputs;
        mPicaPipelineInventory.Observe(
            std::move(entry), draw.CanonicalPipelineId,
            mFrameGraphicsSettingsRevision);
    }
    auto resolvedState = Oot3d::BuildPicaNriPipelineState(
        draw, shader.FragmentOutputs, ActivePicaAttachmentRequirements(),
        mNativePicaSampleCount, mDepthFormat, writesReactiveMask, outlineOcclusionOnly);
    // The superset program declares every guide output; the attachments its
    // specialized program does not declare stay untouched, as they do there.
    for (size_t location = 1U; location < resolvedState.Colors.size(); ++location)
        if ((shader.DeclaredOutputs & (1U << location)) == 0U) resolvedState.Colors[location].colorWriteMask = 0U;
    const auto sourceIdentity = [](const Oot3d::PicaAotShaderSourceIdentity& value) {
        return std::array<uint64_t, 3>{value.Id, value.SecondaryHash, value.Size};
    };
    const Renderer3ds::PicaPipelineProgramIdentity programs{
        sourceIdentity(shader.VertexSource), sourceIdentity(shader.FragmentSource),
        sourceIdentity(shader.NriFragmentSource), shader.NriDescriptorContract};
    auto key = Renderer3ds::BuildPicaPipelineIdentity(
        programs, draw, resolvedState, mPicaDynamicRenderingScope.Available());
    auto [found, inserted] = mNativePicaPipelines.try_emplace(std::move(key));
    auto& record = found->second;
    if (inserted) record.Id = {mNextPicaPipelineId++};
    if (record.Vulkan != VK_NULL_HANDLE ||
        (!requireVulkan && mNriPicaPipelineBridge.OwnedPipelineReady(record.Id))) {
        return record;
    }
    if (!requireVulkan && record.NriPreparationAttempted) {
        // A creation of this record already started; Failed falls back below.
        const auto state = acquisition == NativePicaPipelineAcquisition::Background
            ? mNriPicaPipelineBridge.PollOwnedPipeline(record.Id)
            : mNriPicaPipelineBridge.CompleteOwnedPipeline(record.Id);
        if (state == Oot3d::NriPicaPipelineRequest::Ready) return record;
        if (state == Oot3d::NriPicaPipelineRequest::Pending) {
            if (pending != nullptr) *pending = true;
            return record;
        }
        if (acquisition == NativePicaPipelineAcquisition::Libraries) return record;
    }

    std::vector<VkVertexInputBindingDescription> bindings;
    bindings.reserve(draw.VertexBindings.size());
    for (const auto& binding : draw.VertexBindings) {
        if (binding.ByteStride == 0 || binding.Bytes.empty()) {
            throw std::runtime_error("native PICA vertex binding is empty");
        }
        bindings.push_back({binding.Binding, binding.ByteStride,
                            binding.PerInstance
                                ? VK_VERTEX_INPUT_RATE_INSTANCE
                                : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(draw.VertexAttributes.size());
    for (const auto& attribute : draw.VertexAttributes) {
        const VkFormat format = ToNativeVkVertexFormat(
            attribute.Format, attribute.ComponentCount);
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format,
                                            &properties);
        if ((properties.bufferFeatures &
             VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0) {
            throw std::runtime_error(
                "native PICA vertex format is not supported by this GPU");
        }
        attributes.push_back({attribute.Location, attribute.Binding, format,
                              attribute.ByteOffset});
    }
    std::vector<Oot3d::PicaNriSourceVertexBinding> nriSourceBindings;
    nriSourceBindings.reserve(draw.VertexBindings.size());
    for (const auto& binding : draw.VertexBindings) {
        nriSourceBindings.push_back(
            {binding.Binding, binding.ByteStride, binding.PerInstance});
    }
    std::vector<Oot3d::PicaNriSourceVertexAttribute> nriSourceAttributes;
    nriSourceAttributes.reserve(draw.VertexAttributes.size());
    for (const auto& attribute : draw.VertexAttributes) {
        nriSourceAttributes.push_back({
            attribute.Location, attribute.Binding,
            static_cast<Oot3d::PicaNriVertexScalar>(attribute.Format),
            attribute.ComponentCount, attribute.ByteOffset});
    }
    const auto nriVertexLayout = Oot3d::BuildPicaNriVertexInputLayout(
        nriSourceBindings, nriSourceAttributes);
    if (!nriVertexLayout.Valid())
        throw std::runtime_error(nriVertexLayout.Error);
    std::vector<VkVertexInputBindingDescription> nriBindings;
    nriBindings.reserve(nriVertexLayout.Bindings.size());
    for (const auto& binding : nriVertexLayout.Bindings) {
        nriBindings.push_back({
            binding.Binding, binding.ByteStride,
            binding.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> nriAttributes;
    nriAttributes.reserve(nriVertexLayout.Attributes.size());
    for (const auto& attribute : nriVertexLayout.Attributes) {
        nriAttributes.push_back({
            attribute.Location, attribute.Binding,
            VK_FORMAT_R32G32B32A32_SFLOAT, attribute.ByteOffset});
    }

    if (!record.NriPreparationAttempted && shader.NriDescriptorContract &&
        mNriPicaPipelineBridge.OwnedDrawsEnabled()) {
        record.NriPreparationAttempted = true;
        auto nriPipeline = resolvedState;
        nriPipeline.VertexSpirv = shader.NriVertexSpirv;
        nriPipeline.FragmentSpirv = shader.NriFragmentSpirv;
        nriPipeline.VertexBindings = nriBindings;
        nriPipeline.VertexAttributes = nriAttributes;
        if (Renderer3ds::NriPicaPipelineTraceEnabled())
            std::fprintf(
                stderr,
                "TRIAEVUM_PICA_PIPELINE_REQUEST vs=%016llx fs=%016llx nri_source=%016llx domain=%u applied=%08x "
                "attachments=%02x recorded=%u acquisition=%u\n",
                static_cast<unsigned long long>(Renderer3ds::NriPicaTraceProgramHash(shader.NriVertexSpirv)),
                static_cast<unsigned long long>(Renderer3ds::NriPicaTraceProgramHash(shader.NriFragmentSpirv)),
                static_cast<unsigned long long>(shader.NriFragmentSource.Id), static_cast<unsigned>(domain),
                static_cast<unsigned>(appliedFeatures), static_cast<unsigned>(ActivePicaAttachmentRequirements().Key()),
                recordInventory ? 1U : 0U, static_cast<unsigned>(acquisition));
        if (acquisition == NativePicaPipelineAcquisition::Background && mPreparationQueue) {
            const auto state = mNriPicaPipelineBridge.RequestOwnedPipeline(
                record.Id, nriPipeline, Oot3d::NriPicaPipelineFactory::Default, *mPreparationQueue);
            if (state == Oot3d::NriPicaPipelineRequest::Pending) {
                if (pending != nullptr) *pending = true;
                return record;
            }
            if (state == Oot3d::NriPicaPipelineRequest::Failed)
                SPDLOG_WARN(
                    "NRI PICA pipeline creation failed for shader {}:{}",
                    draw.VertexShaderKey, draw.FragmentShaderKey);
        } else if (!mNriPicaPipelineBridge.CreateOwnedPipeline(
                       record.Id, nriPipeline,
                       acquisition == NativePicaPipelineAcquisition::Libraries
                           ? Oot3d::NriPicaPipelineFactory::Libraries
                           : Oot3d::NriPicaPipelineFactory::Default)) {
            SPDLOG_WARN(
                "NRI PICA pipeline creation failed for shader {}:{}",
                draw.VertexShaderKey, draw.FragmentShaderKey);
        }
    }
    if (!requireVulkan && mNriPicaPipelineBridge.OwnedPipelineReady(record.Id))
        return record;
    // An always-available program never takes the compiling Vulkan fallback.
    if (acquisition == NativePicaPipelineAcquisition::Libraries) return record;

    EnsureNativePicaVulkanShaderModules(draw, shader);
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, shader.VertexShader, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, shader.FragmentShader, "main", nullptr},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount =
        static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = ToNativeVkTopology(draw.Topology);
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.lineWidth = 1.0F;
    rasterization.cullMode = resolvedState.CullMode;
    rasterization.frontFace = resolvedState.FrontFace;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = resolvedState.Samples;
    multisample.alphaToCoverageEnable = resolvedState.AlphaToCoverage;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = resolvedState.DepthTest;
    depthStencil.depthWriteEnable = resolvedState.DepthWrite;
    depthStencil.depthCompareOp = resolvedState.DepthCompare;
    depthStencil.stencilTestEnable = resolvedState.StencilTest;
    depthStencil.front = resolvedState.FrontStencil;
    depthStencil.back = resolvedState.BackStencil;
    const auto& colorAttachments = resolvedState.Colors;
    const uint32_t colorAttachmentCount = resolvedState.ColorAttachmentCount;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.logicOpEnable = resolvedState.LogicOpEnabled;
    colorBlend.logicOp = resolvedState.LogicOp;
    colorBlend.attachmentCount = colorAttachmentCount;
    colorBlend.pAttachments = colorAttachments.data();
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR,
                                         VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount =
        static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(std::size(stages));
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = mNativePicaPipelineLayout;
    const auto& renderingColorFormats = resolvedState.ColorFormats;
    VkPipelineRenderingCreateInfoKHR rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    if (mPicaDynamicRenderingScope.Available()) {
        rendering.colorAttachmentCount = colorAttachmentCount;
        rendering.pColorAttachmentFormats =
            renderingColorFormats.data();
        rendering.depthAttachmentFormat = mDepthFormat;
        if (mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
            mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
            rendering.stencilAttachmentFormat = mDepthFormat;
        pipelineInfo.pNext = &rendering;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
    } else {
        pipelineInfo.renderPass =
            ActivePicaAttachmentRequirements().NativeColorOnly()
                ? mNativePicaCanonicalRenderPass
                : mNativePicaRenderPass;
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    CheckNativeVk(vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1,
                                            &pipelineInfo, nullptr, &pipeline),
                  "vkCreateGraphicsPipelines(native PICA)");
    record.Vulkan = pipeline;
    ++mNativePicaVulkanPipelineCreations;
    return record;
}

void GfxRenderingAPIVulkan::PrewarmNativePicaPipelines() {
    if (!mPicaPipelinePrewarmEnabled ||
        !mPicaPipelineManifest.Loaded() ||
        !mPicaAotShaderPack.Loaded()) {
        return;
    }
    const uint8_t sampleCount =
        static_cast<uint8_t>(mNativePicaSampleCount);
    const auto activeFeatures = Oot3d::ResolvePicaShaderProfileFeatures(
        mFrameGraphicsSettings.Effects, mTemporalMotionEnabled,
        Oot3d::PicaExtensionPassEnabled(
            mPicaExtensionGraph,
            Oot3d::PicaExtensionPass::DirectionalShadowLighting));
    const uint64_t profile =
        static_cast<uint64_t>(ActivePicaAttachmentRequirements().Key()) |
        (static_cast<uint64_t>(sampleCount) << 8U) |
        (static_cast<uint64_t>(activeFeatures) << 16U) |
        (mNativeFidelityProfile ? 1ULL << 48U : 0U);
    if (!mPicaPipelinePrewarmedProfiles.insert(profile).second) {
        return;
    }

    const std::array<uint8_t, 1> dummyVertexBytes{};
    for (const auto& entry : mPicaPipelineManifest.Entries()) {
        if (entry.AttachmentRequirementsKey !=
                ActivePicaAttachmentRequirements().Key() ||
            entry.SampleCount != sampleCount ||
            entry.DescriptorSchemaVersion !=
                mPicaAotShaderPack.DescriptorSchemaVersion() ||
            !entry.MatchesPrewarmProfile(
                activeFeatures, mNativeFidelityProfile)) {
            continue;
        }

        auto& shaderCache =
            entry.Domain == Oot3d::PicaGraphicsPipelineDomain::Canonical
                ? mCanonicalNativePicaShaders
                : mInstrumentedNativePicaShaders;
        const auto shaderKey = Renderer3ds::BuildPicaShaderModuleIdentity(
            {entry.VertexSource.Id, entry.VertexSource.SecondaryHash, entry.VertexSource.Size},
            {entry.FragmentSource.Id, entry.FragmentSource.SecondaryHash, entry.FragmentSource.Size},
            entry.DescriptorSchemaVersion, mNriPicaPipelineBridge.Available(), entry.ShaderOutputs);
        if (mNriPicaPipelineBridge.Available() &&
            !entry.NriFragmentAvailable) {
            ++mPicaPipelinePrewarmSkipped;
            continue;
        }
        auto shaderIt = shaderCache.find(shaderKey);
        if (shaderIt != shaderCache.end() &&
            (shaderIt->second.VertexSource != entry.VertexSource ||
             shaderIt->second.FragmentSource != entry.FragmentSource ||
             shaderIt->second.NriDescriptorContract !=
                 (entry.NriFragmentAvailable &&
                  mNriPicaPipelineBridge.Available()) ||
             (shaderIt->second.NriDescriptorContract &&
              shaderIt->second.NriFragmentSource !=
                  entry.NriFragmentSource) ||
             shaderIt->second.FragmentOutputs != entry.ShaderOutputs)) {
            ++mPicaPipelinePrewarmSkipped;
            continue;
        }
        if (shaderIt == shaderCache.end()) {
            const auto vertexSpirv = mPicaAotShaderPack.Find(
                Oot3d::PicaAotShaderStage::Vertex, entry.VertexSource);
            const auto fragmentSpirv = mPicaAotShaderPack.Find(
                Oot3d::PicaAotShaderStage::Fragment,
                entry.FragmentSource);
            if (vertexSpirv.empty() || fragmentSpirv.empty()) {
                ++mPicaPipelinePrewarmSkipped;
                continue;
            }
            std::span<const uint32_t> nriFragmentSpirv;
            if (entry.NriFragmentAvailable &&
                mNriPicaPipelineBridge.Available()) {
                nriFragmentSpirv = mPicaAotShaderPack.Find(
                    Oot3d::PicaAotShaderStage::NriFragment,
                    entry.NriFragmentSource);
                if (nriFragmentSpirv.empty()) {
                    ++mPicaPipelinePrewarmSkipped;
                    continue;
                }
            }

            NativePicaShaderProgram shader;
            shader.VertexSource = entry.VertexSource;
            shader.FragmentSource = entry.FragmentSource;
            shader.NriFragmentSource = entry.NriFragmentSource;
            shader.FragmentOutputs = entry.ShaderOutputs;
            try {
                shader.NriVertexSpirv.assign(vertexSpirv.begin(),
                                             vertexSpirv.end());
                shader.VertexShader =
                    CreateShaderModuleFromSpirv(vertexSpirv);
                shader.FragmentShader =
                    CreateShaderModuleFromSpirv(fragmentSpirv);
                ++mNativePicaVulkanShaderPairCreations;
                if (!nriFragmentSpirv.empty()) {
                    shader.NriFragmentSpirv.assign(
                        nriFragmentSpirv.begin(), nriFragmentSpirv.end());
                    shader.NriDescriptorContract = true;
                }
                shaderIt = shaderCache.emplace(shaderKey,
                                               std::move(shader)).first;
            } catch (const std::exception& exception) {
                if (shader.FragmentShader != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(mDevice, shader.FragmentShader,
                                          nullptr);
                }
                if (shader.VertexShader != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(mDevice, shader.VertexShader,
                                          nullptr);
                }
                ++mPicaPipelinePrewarmSkipped;
                SPDLOG_WARN(
                    "Native PICA pipeline prewarm shader creation failed: {}",
                    exception.what());
                continue;
            }
        }

        std::vector<GfxNativePicaVertexBindingView> bindings;
        bindings.reserve(entry.VertexBindings.size());
        for (const auto& source : entry.VertexBindings) {
            bindings.push_back({
                source.Binding, source.ByteStride, source.PerInstance,
                dummyVertexBytes});
        }
        std::vector<GfxNativePicaVertexAttributeView> attributes;
        attributes.reserve(entry.VertexAttributes.size());
        for (const auto& source : entry.VertexAttributes) {
            attributes.push_back({
                source.Location, source.Binding, source.Format,
                source.ComponentCount, source.ByteOffset});
        }
        GfxNativePicaDrawView draw;
        draw.CanonicalDescriptorSchemaVersion =
            entry.DescriptorSchemaVersion;
        if (!entry.CanonicalPipelineIds.empty()) {
            draw.CanonicalPipelineId =
                *entry.CanonicalPipelineIds.begin();
        }
        draw.VertexShaderKey = entry.VertexShaderKey;
        draw.FragmentShaderKey = entry.FragmentShaderKey;
        draw.VertexBindings = bindings;
        draw.VertexAttributes = attributes;
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

        const size_t pipelineCount = mNativePicaPipelines.size();
        try {
            GetOrCreateNativePicaPipeline(
                draw, shaderIt->second, entry.WritesReactiveMask,
                entry.Domain ==
                        Oot3d::PicaGraphicsPipelineDomain::Canonical
                    ? Oot3d::PicaShaderDomain::Canonical
                    : Oot3d::PicaShaderDomain::Instrumented,
                entry.RequestedFeatures, entry.AppliedFeatures,
                false, entry.OutlineOcclusionOnly);
            if (mNativePicaPipelines.size() == pipelineCount) {
                ++mPicaPipelinePrewarmReused;
            } else {
                ++mPicaPipelinePrewarmCreated;
            }
        } catch (const std::exception& exception) {
            ++mPicaPipelinePrewarmSkipped;
            SPDLOG_WARN("Native PICA pipeline prewarm failed: {}",
                        exception.what());
        }
    }
}

} // namespace Fast

#endif
