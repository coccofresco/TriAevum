// Native PICA on Vulkan: shader modules, programs, pipelines and prewarm.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/renderer3ds/pica_vrs.h"

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
        Extension::PicaAotShaderStage::Fragment, false, "native_pica_fallback.frag");
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
                preparation.VertexSource, Extension::PicaAotShaderStage::Vertex, true,
                (preparation.Stem + ".vert").c_str());
    } catch (const std::exception& exception) {
        preparation.CompileError = exception.what();
        preparation.ResolveVertex = false;
        preparation.Nri = false;
        preparation.VertexSource = "!";
        return;
    }
    if (!preparation.Nri) return;
    const auto nriVariant = Extension::BuildPicaNriFragmentShaderVariant(preparation.FragmentSource);
    preparation.NriApplied = nriVariant.Applied;
    if (!nriVariant.Applied) {
        preparation.NriError = nriVariant.Error;
        return;
    }
    preparation.NriFragmentSource = nriVariant.Source;
    try {
        preparation.NriFragmentSpirv = ResolveNativePicaShaderSpirv(
            nriVariant.Source, Extension::PicaAotShaderStage::NriFragment, false,
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
    shader.NriFragmentSource = Extension::IdentifyPicaAotShaderSource(preparation->NriFragmentSource);
    mPicaEffectiveShaderInventory.Observe(
        Extension::PicaAotShaderStage::NriFragment, preparation->NriFragmentSource,
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
    Extension::PicaShaderDomain domain,
    Extension::PicaShaderInstrumentationFeature requestedFeatures,
    Extension::PicaShaderInstrumentationFeature appliedFeatures,
    bool recordInventory, bool outlineOcclusionOnly, bool requireVulkan,
    NativePicaPipelineAcquisition acquisition, bool* pending) {
    if (pending != nullptr) *pending = false;
    if (recordInventory && mPicaPipelineInventory.Enabled()) {
        auto entry = Extension::DescribePicaGraphicsPipelineDraw(draw);
        entry.DescriptorSchemaVersion =
            draw.CanonicalDescriptorSchemaVersion;
        entry.Domain = domain == Extension::PicaShaderDomain::Canonical
            ? Extension::PicaGraphicsPipelineDomain::Canonical
            : Extension::PicaGraphicsPipelineDomain::Instrumented;
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
    auto resolvedState = Extension::BuildPicaNriPipelineState(
        draw, shader.FragmentOutputs, ActivePicaAttachmentRequirements(),
        mNativePicaSampleCount, mDepthFormat, writesReactiveMask, outlineOcclusionOnly,
        mNativePicaDrawGeometryGuideAtReset, requestedFeatures, shader.EffectBlock);
    // The superset program declares every guide output; the attachments its
    // specialized program does not declare stay untouched, as they do there.
    for (size_t location = 1U; location < resolvedState.Colors.size(); ++location)
        if ((shader.DeclaredOutputs & (1U << location)) == 0U) resolvedState.Colors[location].colorWriteMask = 0U;
    // Opaque depth prepass eligibility of the resolved state (FlushNativePicaPrepass).
    uint64_t prepassSignature = 0;
    uint8_t prepassIneligible = 0;
    {
        const VkCompareOp compare = resolvedState.DepthCompare;
        bool blends = false;
        uint64_t masks = 0;
        for (size_t location = 0; location < resolvedState.Colors.size(); ++location) {
            // ONE/ZERO with ADD replaces the destination exactly (no read).
            const auto& state = resolvedState.Colors[location];
            const bool replaces = state.colorBlendOp == VK_BLEND_OP_ADD && state.alphaBlendOp == VK_BLEND_OP_ADD &&
                state.srcColorBlendFactor == VK_BLEND_FACTOR_ONE && state.dstColorBlendFactor == VK_BLEND_FACTOR_ZERO &&
                state.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE && state.dstAlphaBlendFactor == VK_BLEND_FACTOR_ZERO;
            blends = blends || (state.blendEnable != VK_FALSE && state.colorWriteMask != 0U && !replaces);
            masks |= static_cast<uint64_t>(resolvedState.Colors[location].colorWriteMask & 0xfU) << (4U * location);
        }
        if (!resolvedState.DepthTest || !resolvedState.DepthWrite) prepassIneligible = 1;
        else if (compare != VK_COMPARE_OP_LESS_OR_EQUAL && compare != VK_COMPARE_OP_GREATER_OR_EQUAL &&
                 compare != VK_COMPARE_OP_LESS && compare != VK_COMPARE_OP_GREATER)
            prepassIneligible = 4;
        else if (resolvedState.StencilTest) prepassIneligible = 5;
        else if (blends) prepassIneligible = 6;
        else if (resolvedState.LogicOpEnabled && resolvedState.LogicOp != VK_LOGIC_OP_COPY) prepassIneligible = 7;
        else if (resolvedState.AlphaToCoverage || resolvedState.Samples != VK_SAMPLE_COUNT_1_BIT) prepassIneligible = 8;
        // Strict compare without culling: a two-sided surface drawn as two
        // coincident triangles of one draw has two fragments at the same depth
        // per pixel, where LESS keeps the first and the EQUAL pass the last.
        else if ((compare == VK_COMPARE_OP_LESS || compare == VK_COMPARE_OP_GREATER) &&
                 resolvedState.CullMode == VK_CULL_MODE_NONE)
            prepassIneligible = 12;
        else prepassSignature = masks | (static_cast<uint64_t>(compare) << 40U) | (1ULL << 63U);
    }
    // Prepass variants of the same program and vertex state: depth-only (no
    // colour or guide write) and the colour pass (compare EQUAL, no depth write).
    if (mNativePicaPrepassVariant == 1U || mNativePicaPrepassVariant == 3U) {
        for (auto& color : resolvedState.Colors) color.colorWriteMask = 0U;
    } else if (mNativePicaPrepassVariant == 2U) {
        resolvedState.DepthCompare = VK_COMPARE_OP_EQUAL;
        resolvedState.DepthWrite = false;
    }
    const auto sourceIdentity = [](const Extension::PicaAotShaderSourceIdentity& value) {
        return std::array<uint64_t, 3>{value.Id, value.SecondaryHash, value.Size};
    };
    static const bool emptyFragment = [] {
        const char* value = std::getenv("TRIAEVUM_DEPTH_PREPASS_EMPTY_FS");
        return value == nullptr || value[0] != '0';
    }();
    const bool emptyFragmentVariant = mNativePicaPrepassVariant == 1U && emptyFragment;
    Renderer3ds::PicaPipelineProgramIdentity programs{
        sourceIdentity(shader.VertexSource), sourceIdentity(shader.FragmentSource),
        sourceIdentity(shader.NriFragmentSource), shader.NriDescriptorContract};
    // The empty fragment stage is another program (and another pipeline).
    if (emptyFragmentVariant)
        programs = {sourceIdentity(shader.VertexSource), {0x656d707479667331ULL, 0U, 128U},
                    {0x656d707479667331ULL, 0U, 128U}, shader.NriDescriptorContract};
    auto key = Renderer3ds::BuildPicaPipelineIdentity(
        programs, draw, resolvedState, mPicaDynamicRenderingScope.Available());
    auto [found, inserted] = mNativePicaPipelines.try_emplace(std::move(key));
    auto& record = found->second;
    if (inserted) {
        record.Id = {mNextPicaPipelineId++};
        record.PrepassSignature = prepassSignature;
        record.PrepassIneligible = prepassIneligible;
    }
    if (record.Vulkan != VK_NULL_HANDLE ||
        (!requireVulkan && mNriPicaPipelineBridge.OwnedPipelineReady(record.Id))) {
        return record;
    }
    if (!requireVulkan && record.NriPreparationAttempted) {
        // A creation of this record already started; Failed falls back below.
        const auto state = acquisition == NativePicaPipelineAcquisition::Background
            ? mNriPicaPipelineBridge.PollOwnedPipeline(record.Id)
            : mNriPicaPipelineBridge.CompleteOwnedPipeline(record.Id);
        if (state == Extension::NriPicaPipelineRequest::Ready) return record;
        if (state == Extension::NriPicaPipelineRequest::Pending) {
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
    bool nativeVertexFormatsSupported = true;
    for (const auto& attribute : draw.VertexAttributes) {
        const VkFormat format = ToNativeVkVertexFormat(
            attribute.Format, attribute.ComponentCount);
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format,
                                            &properties);
        if ((properties.bufferFeatures &
             VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0) {
            // The NRI path repacks such attributes into four floats (mobile
            // GPUs lack three-byte formats); only the legacy pipeline needs them.
            nativeVertexFormatsSupported = false;
        }
        attributes.push_back({attribute.Location, attribute.Binding, format,
                              attribute.ByteOffset});
    }
    std::vector<Extension::PicaNriSourceVertexBinding> nriSourceBindings;
    nriSourceBindings.reserve(draw.VertexBindings.size());
    for (const auto& binding : draw.VertexBindings) {
        nriSourceBindings.push_back(
            {binding.Binding, binding.ByteStride, binding.PerInstance});
    }
    std::vector<Extension::PicaNriSourceVertexAttribute> nriSourceAttributes;
    nriSourceAttributes.reserve(draw.VertexAttributes.size());
    for (const auto& attribute : draw.VertexAttributes) {
        nriSourceAttributes.push_back({
            attribute.Location, attribute.Binding,
            static_cast<Extension::PicaNriVertexScalar>(attribute.Format),
            attribute.ComponentCount, attribute.ByteOffset});
    }
    const auto nriVertexLayout = Extension::BuildPicaNriVertexInputLayout(
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
        // The prepass's depth-only variant writes no colour or guide: an empty
        // fragment stage, so no fragment is shaded. Same vertex SPIR-V and
        // vertex state (TRIAEVUM_DEPTH_PREPASS_EMPTY_FS=0 keeps the program).
        // OpCapability Shader; OpMemoryModel Logical GLSL450; OpEntryPoint
        // Fragment %1 "main"; OpExecutionMode %1 OriginUpperLeft; %2 void;
        // %3 fn(void); %1 = OpFunction; OpLabel; OpReturn; OpFunctionEnd.
        static constexpr uint32_t kEmptyFragmentSpirv[] = {
            0x07230203U, 0x00010000U, 0U, 5U, 0U,
            0x00020011U, 1U,
            0x0003000EU, 0U, 1U,
            0x0005000FU, 4U, 1U, 0x6E69616DU, 0U,
            0x00030010U, 1U, 7U,
            0x00020013U, 2U,
            0x00030021U, 3U, 2U,
            0x00050036U, 2U, 1U, 0U, 3U,
            0x000200F8U, 4U,
            0x000100FDU,
            0x00010038U,
        };
        if (emptyFragmentVariant) nriPipeline.FragmentSpirv = kEmptyFragmentSpirv;
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
                record.Id, nriPipeline, Extension::NriPicaPipelineFactory::Default, *mPreparationQueue);
            if (state == Extension::NriPicaPipelineRequest::Pending) {
                if (pending != nullptr) *pending = true;
                return record;
            }
            if (state == Extension::NriPicaPipelineRequest::Failed)
                SPDLOG_WARN(
                    "NRI PICA pipeline creation failed for shader {}:{}",
                    draw.VertexShaderKey, draw.FragmentShaderKey);
        } else if (!mNriPicaPipelineBridge.CreateOwnedPipeline(
                       record.Id, nriPipeline,
                       acquisition == NativePicaPipelineAcquisition::Libraries
                           ? Extension::NriPicaPipelineFactory::Libraries
                           : Extension::NriPicaPipelineFactory::Default)) {
            SPDLOG_WARN(
                "NRI PICA pipeline creation failed for shader {}:{}",
                draw.VertexShaderKey, draw.FragmentShaderKey);
        }
    }
    if (!requireVulkan && mNriPicaPipelineBridge.OwnedPipelineReady(record.Id))
        return record;
    // An always-available program never takes the compiling Vulkan fallback.
    if (acquisition == NativePicaPipelineAcquisition::Libraries) return record;

    if (!nativeVertexFormatsSupported)
        throw std::runtime_error("native PICA vertex format is not supported by this GPU and the "
                                 "NRI pipeline is unavailable");
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
    // The fragment shading rate is dynamic only with VRS on (pica_vrs.h).
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR,
                                         VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                         VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount =
        static_cast<uint32_t>(std::size(dynamicStates)) - (Renderer3ds::PicaVrs::Dynamic() ? 0U : 1U);
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
    // A device lost earlier surfaces here first: CheckVk prints the fault
    // report and breadcrumb trail before throwing.
    CheckVk(vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1,
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
    const auto activeFeatures = Extension::ResolvePicaShaderProfileFeatures(
        mFrameGraphicsSettings.Effects, mTemporalMotionEnabled,
        Extension::PicaExtensionPassEnabled(
            mPicaExtensionGraph,
            Extension::PicaExtensionPass::DirectionalShadowLighting));
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
            entry.Domain == Extension::PicaGraphicsPipelineDomain::Canonical
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
                Extension::PicaAotShaderStage::Vertex, entry.VertexSource);
            const auto fragmentSpirv = mPicaAotShaderPack.Find(
                Extension::PicaAotShaderStage::Fragment,
                entry.FragmentSource);
            if (vertexSpirv.empty() || fragmentSpirv.empty()) {
                ++mPicaPipelinePrewarmSkipped;
                continue;
            }
            std::span<const uint32_t> nriFragmentSpirv;
            if (entry.NriFragmentAvailable &&
                mNriPicaPipelineBridge.Available()) {
                nriFragmentSpirv = mPicaAotShaderPack.Find(
                    Extension::PicaAotShaderStage::NriFragment,
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
                        Extension::PicaGraphicsPipelineDomain::Canonical
                    ? Extension::PicaShaderDomain::Canonical
                    : Extension::PicaShaderDomain::Instrumented,
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
