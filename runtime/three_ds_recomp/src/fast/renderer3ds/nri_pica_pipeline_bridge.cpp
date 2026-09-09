#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"

#ifdef ENABLE_RENDERER3DS_VULKAN

#include "fast/renderer3ds/pica_nri_shader_contract.h"
#include "fast/renderer3ds/pica_nri_upload.h"

#ifdef ENABLE_RENDERER3DS_NRI
#include "nri_pica_upload_arena.h"
#include <Extensions/NRIHelper.h>
#include <NRI.h>
#endif

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

namespace Fast::Renderer3ds {
namespace {

#ifdef ENABLE_RENDERER3DS_NRI
nri::Topology ToNriTopology(VkPrimitiveTopology topology) {
    switch (topology) {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            return nri::Topology::TRIANGLE_LIST;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return nri::Topology::TRIANGLE_STRIP;
        default:
            return nri::Topology::MAX_NUM;
    }
}

nri::CullMode ToNriCullMode(VkCullModeFlags mode) {
    switch (mode) {
        case VK_CULL_MODE_NONE: return nri::CullMode::NONE;
        case VK_CULL_MODE_FRONT_BIT: return nri::CullMode::FRONT;
        case VK_CULL_MODE_BACK_BIT: return nri::CullMode::BACK;
        default: return nri::CullMode::MAX_NUM;
    }
}

nri::CompareOp ToNriCompare(VkCompareOp operation) {
    switch (operation) {
        case VK_COMPARE_OP_NEVER: return nri::CompareOp::NEVER;
        case VK_COMPARE_OP_LESS: return nri::CompareOp::LESS;
        case VK_COMPARE_OP_EQUAL: return nri::CompareOp::EQUAL;
        case VK_COMPARE_OP_LESS_OR_EQUAL: return nri::CompareOp::LESS_EQUAL;
        case VK_COMPARE_OP_GREATER: return nri::CompareOp::GREATER;
        case VK_COMPARE_OP_NOT_EQUAL: return nri::CompareOp::NOT_EQUAL;
        case VK_COMPARE_OP_GREATER_OR_EQUAL:
            return nri::CompareOp::GREATER_EQUAL;
        case VK_COMPARE_OP_ALWAYS: return nri::CompareOp::ALWAYS;
        default: return nri::CompareOp::NONE;
    }
}

nri::StencilOp ToNriStencil(VkStencilOp operation) {
    switch (operation) {
        case VK_STENCIL_OP_KEEP: return nri::StencilOp::KEEP;
        case VK_STENCIL_OP_ZERO: return nri::StencilOp::ZERO;
        case VK_STENCIL_OP_REPLACE: return nri::StencilOp::REPLACE;
        case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
            return nri::StencilOp::INCREMENT_AND_CLAMP;
        case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
            return nri::StencilOp::DECREMENT_AND_CLAMP;
        case VK_STENCIL_OP_INVERT: return nri::StencilOp::INVERT;
        case VK_STENCIL_OP_INCREMENT_AND_WRAP:
            return nri::StencilOp::INCREMENT_AND_WRAP;
        case VK_STENCIL_OP_DECREMENT_AND_WRAP:
            return nri::StencilOp::DECREMENT_AND_WRAP;
        default: return nri::StencilOp::KEEP;
    }
}

nri::BlendFactor ToNriBlendFactor(VkBlendFactor factor) {
    switch (factor) {
        case VK_BLEND_FACTOR_ZERO: return nri::BlendFactor::ZERO;
        case VK_BLEND_FACTOR_ONE: return nri::BlendFactor::ONE;
        case VK_BLEND_FACTOR_SRC_COLOR: return nri::BlendFactor::SRC_COLOR;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return nri::BlendFactor::ONE_MINUS_SRC_COLOR;
        case VK_BLEND_FACTOR_DST_COLOR: return nri::BlendFactor::DST_COLOR;
        case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return nri::BlendFactor::ONE_MINUS_DST_COLOR;
        case VK_BLEND_FACTOR_SRC_ALPHA: return nri::BlendFactor::SRC_ALPHA;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
        case VK_BLEND_FACTOR_DST_ALPHA: return nri::BlendFactor::DST_ALPHA;
        case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return nri::BlendFactor::ONE_MINUS_DST_ALPHA;
        case VK_BLEND_FACTOR_CONSTANT_COLOR:
            return nri::BlendFactor::CONSTANT_COLOR;
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
            return nri::BlendFactor::ONE_MINUS_CONSTANT_COLOR;
        case VK_BLEND_FACTOR_CONSTANT_ALPHA:
            return nri::BlendFactor::CONSTANT_ALPHA;
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
            return nri::BlendFactor::ONE_MINUS_CONSTANT_ALPHA;
        case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
            return nri::BlendFactor::SRC_ALPHA_SATURATE;
        case VK_BLEND_FACTOR_SRC1_COLOR:
            return nri::BlendFactor::SRC1_COLOR;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
            return nri::BlendFactor::ONE_MINUS_SRC1_COLOR;
        case VK_BLEND_FACTOR_SRC1_ALPHA:
            return nri::BlendFactor::SRC1_ALPHA;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
            return nri::BlendFactor::ONE_MINUS_SRC1_ALPHA;
        default: return nri::BlendFactor::ZERO;
    }
}

nri::BlendOp ToNriBlendOp(VkBlendOp operation) {
    switch (operation) {
        case VK_BLEND_OP_ADD: return nri::BlendOp::ADD;
        case VK_BLEND_OP_SUBTRACT: return nri::BlendOp::SUBTRACT;
        case VK_BLEND_OP_REVERSE_SUBTRACT:
            return nri::BlendOp::REVERSE_SUBTRACT;
        case VK_BLEND_OP_MIN: return nri::BlendOp::MIN;
        case VK_BLEND_OP_MAX: return nri::BlendOp::MAX;
        default: return nri::BlendOp::ADD;
    }
}

bool ReferencesBlendConstants(VkBlendFactor factor) {
    return factor == VK_BLEND_FACTOR_CONSTANT_COLOR ||
           factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR ||
           factor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
           factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
}

nri::LogicOp ToNriLogic(VkLogicOp operation) {
    switch (operation) {
        case VK_LOGIC_OP_CLEAR: return nri::LogicOp::CLEAR;
        case VK_LOGIC_OP_AND: return nri::LogicOp::AND;
        case VK_LOGIC_OP_AND_REVERSE: return nri::LogicOp::AND_REVERSE;
        case VK_LOGIC_OP_COPY: return nri::LogicOp::COPY;
        case VK_LOGIC_OP_AND_INVERTED: return nri::LogicOp::AND_INVERTED;
        case VK_LOGIC_OP_NO_OP: return nri::LogicOp::NONE;
        case VK_LOGIC_OP_XOR: return nri::LogicOp::XOR;
        case VK_LOGIC_OP_OR: return nri::LogicOp::OR;
        case VK_LOGIC_OP_NOR: return nri::LogicOp::NOR;
        case VK_LOGIC_OP_EQUIVALENT: return nri::LogicOp::EQUIVALENT;
        case VK_LOGIC_OP_INVERT: return nri::LogicOp::INVERT;
        case VK_LOGIC_OP_OR_REVERSE: return nri::LogicOp::OR_REVERSE;
        case VK_LOGIC_OP_COPY_INVERTED:
            return nri::LogicOp::COPY_INVERTED;
        case VK_LOGIC_OP_OR_INVERTED:
            return nri::LogicOp::OR_INVERTED;
        case VK_LOGIC_OP_NAND: return nri::LogicOp::NAND;
        case VK_LOGIC_OP_SET: return nri::LogicOp::SET;
        default: return nri::LogicOp::NONE;
    }
}

nri::StencilDesc ToNriStencilDesc(const VkStencilOpState& state) {
    nri::StencilDesc result{};
    result.compareOp = ToNriCompare(state.compareOp);
    result.failOp = ToNriStencil(state.failOp);
    result.passOp = ToNriStencil(state.passOp);
    result.depthFailOp = ToNriStencil(state.depthFailOp);
    result.writeMask = static_cast<uint8_t>(state.writeMask);
    result.compareMask = static_cast<uint8_t>(state.compareMask);
    return result;
}

nri::Filter ToNriFilter(VkFilter filter) {
    return filter == VK_FILTER_LINEAR ? nri::Filter::LINEAR
                                      : nri::Filter::NEAREST;
}

nri::AddressMode ToNriAddressMode(VkSamplerAddressMode mode) {
    switch (mode) {
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return nri::AddressMode::CLAMP_TO_EDGE;
        case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return nri::AddressMode::MIRRORED_REPEAT;
        default:
            return nri::AddressMode::REPEAT;
    }
}
#endif

} // namespace

struct NriPicaSamplerKey {
    VkFilter MinFilter = VK_FILTER_NEAREST;
    VkFilter MagFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode AddressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode AddressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float MipBias = 0.0F;
    float MinLod = 0.0F;
    float MaxLod = 0.0F;
    bool Integer = false;

    auto operator<=>(const NriPicaSamplerKey&) const = default;
};

struct NriPicaPipelineBridge::Impl {
    NriPicaInterop* Interop = nullptr;
    std::string Reason = "NRI PICA pipeline bridge is not initialized";
#ifdef ENABLE_RENDERER3DS_NRI
    std::unordered_map<VkPipeline, nri::Pipeline*> Pipelines;
    std::unordered_map<VkPipeline, nri::Pipeline*> OwnedPipelines;
    std::unordered_map<VkPipeline, bool> OwnedPipelineUsesBlendConstants;
    nri::PipelineLayout* DescriptorLayout = nullptr;
    nri::PipelineCache* PipelineCache = nullptr;
    bool PipelineCacheAttempted = false;
    struct FrameDescriptors {
        nri::DescriptorPool* Pool = nullptr;
        std::vector<nri::DescriptorSet*> Sets;
        std::vector<nri::Descriptor*> TransientViews;
        std::unique_ptr<NriPicaUploadArena> UploadArena;
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        uint32_t Used = 0;
    };
    std::vector<FrameDescriptors> Frames;
    std::map<NriPicaSamplerKey, nri::Descriptor*> Samplers;
    bool OwnedDraws = false;
    bool PreferOwnedUploads = false;
    bool LastDrawUploadsOwned = false;
    uint64_t LastDrawUploadBytes = 0;
#endif
};

NriPicaPipelineBridge::NriPicaPipelineBridge()
    : mImpl(std::make_unique<Impl>()) {}
NriPicaPipelineBridge::~NriPicaPipelineBridge() { Shutdown(); }

bool NriPicaPipelineBridge::Initialize(
    NriPicaInterop& interop,
    const NriPicaExecutionConfig& config) {
    Shutdown();
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_RENDERER3DS_NRI
    mImpl->Reason = "NRI PICA pipeline bridge was not compiled";
    return false;
#else
    if (config.FrameSlotCount == 0U ||
        config.MaxDrawsPerFrame == 0U) {
        mImpl->Reason = "NRI PICA execution configuration is invalid";
        return false;
    }
    mImpl->Interop = &interop;
    mImpl->Frames.resize(config.FrameSlotCount);
    std::array<nri::DescriptorRangeDesc,
               kPicaNriDescriptorBindings.size()> ranges{};
    const auto descriptorType = [](PicaNriDescriptorKind kind) {
        switch (kind) {
            case PicaNriDescriptorKind::ConstantBuffer:
                return nri::DescriptorType::CONSTANT_BUFFER;
            case PicaNriDescriptorKind::Texture:
                return nri::DescriptorType::TEXTURE;
            case PicaNriDescriptorKind::StorageTexture:
                return nri::DescriptorType::STORAGE_TEXTURE;
            case PicaNriDescriptorKind::Sampler:
                return nri::DescriptorType::SAMPLER;
        }
        return nri::DescriptorType::MUTABLE;
    };
    for (size_t index = 0; index < ranges.size(); ++index) {
        const auto& binding = kPicaNriDescriptorBindings[index];
        ranges[index] = {
            binding.Binding, 1U, descriptorType(binding.Kind),
            binding.Stage == PicaNriShaderStage::Vertex
                ? nri::StageBits::VERTEX_SHADER
                : nri::StageBits::FRAGMENT_SHADER};
    }
    nri::DescriptorSetDesc descriptorSet{};
    descriptorSet.registerSpace = 0;
    descriptorSet.ranges = ranges.data();
    descriptorSet.rangeNum = static_cast<uint32_t>(ranges.size());
    nri::RootConstantDesc rootConstants{};
    rootConstants.registerIndex = 0;
    rootConstants.size = 8U * sizeof(float);
    rootConstants.shaderStages =
        nri::StageBits::VERTEX_SHADER |
        nri::StageBits::FRAGMENT_SHADER;
    nri::PipelineLayoutDesc layout{};
    layout.rootRegisterSpace = 1;
    layout.rootConstants = &rootConstants;
    layout.rootConstantNum = 1;
    layout.descriptorSets = &descriptorSet;
    layout.descriptorSetNum = 1;
    layout.shaderStages =
        nri::StageBits::VERTEX_SHADER |
        nri::StageBits::FRAGMENT_SHADER;
    layout.flags =
        nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
    nri::CoreInterface* core = interop.Core();
    nri::Device* device = interop.Device();
    if (!ValidatePicaNriDescriptorContract() || core == nullptr ||
        device == nullptr ||
        core->CreatePipelineLayout(
            *device, layout, mImpl->DescriptorLayout) !=
            nri::Result::SUCCESS) {
        mImpl->Reason =
            "NRI PICA descriptor pipeline layout creation failed";
        mImpl->Interop = nullptr;
        return false;
    }
    mImpl->OwnedDraws = config.OwnedDraws;
    mImpl->PreferOwnedUploads = config.PreferOwnedUploads;
    if (mImpl->OwnedDraws) {
        for (auto& frame : mImpl->Frames) {
            nri::DescriptorPoolDesc pool{};
            pool.descriptorSetMaxNum = config.MaxDrawsPerFrame;
            pool.constantBufferMaxNum =
                4U * config.MaxDrawsPerFrame;
            pool.textureMaxNum = 5U * config.MaxDrawsPerFrame;
            pool.storageTextureMaxNum = config.MaxDrawsPerFrame;
            pool.samplerMaxNum = 5U * config.MaxDrawsPerFrame;
            frame.Sets.resize(config.MaxDrawsPerFrame);
            if (core->CreateDescriptorPool(
                    *device, pool, frame.Pool) !=
                    nri::Result::SUCCESS ||
                core->AllocateDescriptorSets(
                    *frame.Pool, *mImpl->DescriptorLayout, 0,
                    frame.Sets.data(),
                    static_cast<uint32_t>(frame.Sets.size()), 0) !=
                    nri::Result::SUCCESS) {
                mImpl->OwnedDraws = false;
                break;
            }
        }
        if (!mImpl->OwnedDraws) {
            for (auto& frame : mImpl->Frames) {
                if (frame.Pool != nullptr)
                    core->DestroyDescriptorPool(frame.Pool);
                frame = {};
            }
        }
    }
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriPicaPipelineBridge::InitializePipelineCache(std::span<const uint8_t> data) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)data;
    return false;
#else
    if (!Available()) return false;
    if (mImpl->PipelineCacheAttempted) return mImpl->PipelineCache != nullptr;
    mImpl->PipelineCacheAttempted = true;
    nri::PipelineCacheDesc desc{};
    desc.data = data.data();
    desc.size = data.size();
    auto* core = mImpl->Interop->Core();
    auto result = core->CreatePipelineCache(*mImpl->Interop->Device(), desc, mImpl->PipelineCache);
    if (result != nri::Result::SUCCESS && !data.empty()) {
        mImpl->PipelineCache = nullptr;
        desc = {};
        result = core->CreatePipelineCache(*mImpl->Interop->Device(), desc, mImpl->PipelineCache);
    }
    if (result != nri::Result::SUCCESS) mImpl->PipelineCache = nullptr;
    return mImpl->PipelineCache != nullptr;
#endif
}

std::vector<uint8_t> NriPicaPipelineBridge::GetPipelineCacheData() const {
#ifndef ENABLE_RENDERER3DS_NRI
    return {};
#else
    if (!Available() || mImpl->PipelineCache == nullptr) return {};
    auto* core = mImpl->Interop->Core();
    uint64_t size = 0;
    if (core->GetPipelineCacheData(*mImpl->PipelineCache, nullptr, size) != nri::Result::SUCCESS ||
        size == 0 || size > 64ULL * 1024ULL * 1024ULL) return {};
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (core->GetPipelineCacheData(*mImpl->PipelineCache, data.data(), size) != nri::Result::SUCCESS ||
        size == 0 || size > data.size()) return {};
    data.resize(static_cast<size_t>(size));
    return data;
#endif
}

bool NriPicaPipelineBridge::CreateOwnedPipeline(
    VkPipeline fallbackPipeline,
    const NriPicaGraphicsPipelineDesc& desc) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)fallbackPipeline;
    (void)desc;
    return false;
#else
    if (!Available() || fallbackPipeline == VK_NULL_HANDLE ||
        desc.VertexSpirv.empty() || desc.FragmentSpirv.empty() ||
        desc.ColorAttachmentCount == 0U ||
        desc.ColorAttachmentCount > kPicaColorAttachmentCount ||
        desc.VertexBindings.size() >
            std::numeric_limits<uint8_t>::max() ||
        desc.VertexAttributes.size() >
            std::numeric_limits<uint8_t>::max() ||
        mImpl->OwnedPipelines.contains(fallbackPipeline))
        return false;

    const nri::Topology topology = ToNriTopology(desc.Topology);
    const nri::CullMode cullMode = ToNriCullMode(desc.CullMode);
    if (topology == nri::Topology::MAX_NUM ||
        cullMode == nri::CullMode::MAX_NUM)
        return false;

    std::vector<nri::VertexStreamDesc> streams;
    streams.reserve(desc.VertexBindings.size());
    for (const auto& binding : desc.VertexBindings) {
        if (binding.binding >
                std::numeric_limits<uint16_t>::max() ||
            binding.stride >
                std::numeric_limits<uint16_t>::max())
            return false;
        streams.push_back({
            static_cast<uint16_t>(binding.binding),
            binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE
                ? nri::VertexStreamStepRate::PER_INSTANCE
                : nri::VertexStreamStepRate::PER_VERTEX,
            static_cast<uint16_t>(binding.stride)});
    }
    std::vector<nri::VertexAttributeDesc> attributes;
    attributes.reserve(desc.VertexAttributes.size());
    for (const auto& attribute : desc.VertexAttributes) {
        if (attribute.binding >
                std::numeric_limits<uint16_t>::max())
            return false;
        nri::VertexAttributeDesc converted{};
        converted.vk.location = attribute.location;
        converted.offset = attribute.offset;
        converted.format =
            nri::nriConvertVKFormatToNRI(attribute.format);
        converted.streamIndex =
            static_cast<uint16_t>(attribute.binding);
        if (converted.format == nri::Format::UNKNOWN)
            return false;
        attributes.push_back(converted);
    }
    const nri::VertexInputDesc vertexInput{
        attributes.data(),
        static_cast<uint8_t>(attributes.size()),
        streams.data(),
        static_cast<uint8_t>(streams.size())};

    const std::array<nri::ShaderDesc, 2> shaders{{
        {nri::StageBits::VERTEX_SHADER, desc.VertexSpirv.data(),
         desc.VertexSpirv.size_bytes()},
        {nri::StageBits::FRAGMENT_SHADER, desc.FragmentSpirv.data(),
         desc.FragmentSpirv.size_bytes()},
    }};
    std::array<nri::ColorAttachmentDesc,
               kPicaColorAttachmentCount> colors{};
    for (size_t index = 0; index < desc.ColorAttachmentCount; ++index) {
        const auto& source = desc.Colors[index];
        auto& destination = colors[index];
        destination.format =
            nri::nriConvertVKFormatToNRI(desc.ColorFormats[index]);
        if (destination.format == nri::Format::UNKNOWN)
            return false;
        destination.colorBlend = {
            ToNriBlendFactor(source.srcColorBlendFactor),
            ToNriBlendFactor(source.dstColorBlendFactor),
            ToNriBlendOp(source.colorBlendOp)};
        destination.alphaBlend = {
            ToNriBlendFactor(source.srcAlphaBlendFactor),
            ToNriBlendFactor(source.dstAlphaBlendFactor),
            ToNriBlendOp(source.alphaBlendOp)};
        destination.colorWriteMask =
            static_cast<nri::ColorWriteBits>(
                static_cast<uint8_t>(source.colorWriteMask));
        destination.blendEnabled = source.blendEnable != VK_FALSE;
    }
    const nri::Format depthFormat =
        nri::nriConvertVKFormatToNRI(desc.DepthStencilFormat);
    if (depthFormat == nri::Format::UNKNOWN)
        return false;

    nri::MultisampleDesc multisample{};
    multisample.sampleMask = 0xFFFFFFFFU;
    multisample.sampleNum =
        static_cast<nri::Sample_t>(desc.Samples);
    multisample.alphaToCoverage = desc.AlphaToCoverage;
    nri::GraphicsPipelineDesc pipeline{};
    pipeline.pipelineLayout = mImpl->DescriptorLayout;
    pipeline.vertexInput = &vertexInput;
    pipeline.inputAssembly.topology = topology;
    pipeline.rasterization.fillMode = nri::FillMode::SOLID;
    pipeline.rasterization.cullMode = cullMode;
    pipeline.rasterization.frontCounterClockwise =
        desc.FrontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline.multisample = &multisample;
    pipeline.outputMerger.colors = colors.data();
    pipeline.outputMerger.colorNum = desc.ColorAttachmentCount;
    pipeline.outputMerger.depth.compareOp =
        desc.DepthTest ? ToNriCompare(desc.DepthCompare)
                       : (desc.DepthWrite ? nri::CompareOp::ALWAYS
                                          : nri::CompareOp::NONE);
    pipeline.outputMerger.depth.write = desc.DepthWrite;
    if (desc.StencilTest) {
        pipeline.outputMerger.stencil.front =
            ToNriStencilDesc(desc.FrontStencil);
        pipeline.outputMerger.stencil.back =
            ToNriStencilDesc(desc.BackStencil);
    }
    pipeline.outputMerger.depthStencilFormat = depthFormat;
    pipeline.outputMerger.logicOp =
        desc.LogicOpEnabled ? ToNriLogic(desc.LogicOp)
                            : nri::LogicOp::NONE;
    if (desc.LogicOpEnabled && desc.LogicOp == VK_LOGIC_OP_NO_OP) {
        pipeline.outputMerger.logicOp = nri::LogicOp::NONE;
        for (auto& color : colors)
            color.colorWriteMask = nri::ColorWriteBits::NONE;
    }
    pipeline.shaders = shaders.data();
    pipeline.shaderNum = static_cast<uint32_t>(shaders.size());
    // Reuse driver compilation across layout/raster variants and Reset().
    // Cache failure is non-fatal: the same canonical pipeline remains valid.
    InitializePipelineCache();
    pipeline.cache = mImpl->PipelineCache;
    nri::Pipeline* owned = nullptr;
    if (mImpl->Interop->Core()
            ->CreateGraphicsPipeline(
                *mImpl->Interop->Device(),
                pipeline, owned) != nri::Result::SUCCESS ||
        owned == nullptr)
        return false;
    mImpl->OwnedPipelines.emplace(fallbackPipeline, owned);
    const bool usesBlendConstants = std::any_of(
        desc.Colors.begin(), desc.Colors.end(),
        [](const VkPipelineColorBlendAttachmentState& color) {
            return ReferencesBlendConstants(
                       color.srcColorBlendFactor) ||
                   ReferencesBlendConstants(
                       color.dstColorBlendFactor) ||
                   ReferencesBlendConstants(
                       color.srcAlphaBlendFactor) ||
                   ReferencesBlendConstants(
                       color.dstAlphaBlendFactor);
        });
    mImpl->OwnedPipelineUsesBlendConstants.emplace(
        fallbackPipeline, usesBlendConstants);
    return true;
#endif
}

bool NriPicaPipelineBridge::BindOwnedDraw(
    const NriPicaOwnedDrawDesc& desc) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)desc;
    return false;
#else
    mImpl->LastDrawUploadsOwned = false;
    mImpl->LastDrawUploadBytes = 0;
    if (!Available() || !mImpl->OwnedDraws ||
        desc.FallbackPipeline == VK_NULL_HANDLE ||
        desc.UniformBuffer == VK_NULL_HANDLE ||
        desc.UniformBufferSize == 0U ||
        desc.VertexBuffer == VK_NULL_HANDLE ||
        desc.VertexBufferSize == 0U ||
        desc.VertexBindings.empty() ||
        desc.VertexOrIndexCount == 0U ||
        desc.StorageImage == VK_NULL_HANDLE ||
        desc.StorageFormat == VK_FORMAT_UNDEFINED ||
        desc.StorageWidth == 0U || desc.StorageHeight == 0U)
        return false;
    const auto pipeline =
        mImpl->OwnedPipelines.find(desc.FallbackPipeline);
    nri::CommandBuffer* command =
        mImpl->Interop->CommandBuffer(desc.FrameIndex);
    auto& frame = mImpl->Frames[
        desc.FrameIndex % mImpl->Frames.size()];
    nri::CoreInterface* core = mImpl->Interop->Core();
    nri::Device* device = mImpl->Interop->Device();
    if (pipeline == mImpl->OwnedPipelines.end() ||
        command == nullptr || core == nullptr || device == nullptr ||
        frame.Pool == nullptr)
        return false;
    if (frame.FrameId != desc.FrameId) {
        for (nri::Descriptor* view : frame.TransientViews)
            core->DestroyDescriptor(view);
        frame.TransientViews.clear();
        frame.FrameId = desc.FrameId;
        frame.Used = 0;
    }
    if (frame.Used >= frame.Sets.size())
        return false;

    nri::Buffer* uniformBuffer = nullptr;
    nri::Buffer* vertexBuffer = nullptr;
    if (mImpl->PreferOwnedUploads &&
        frame.UploadArena == nullptr)
        frame.UploadArena =
            std::make_unique<NriPicaUploadArena>();
    if (mImpl->PreferOwnedUploads &&
        !desc.VertexContentPersistent &&
        desc.UniformMappedMemory != nullptr &&
        desc.VertexMappedMemory != nullptr &&
        desc.UniformBufferSize <=
            std::numeric_limits<size_t>::max() &&
        desc.VertexBufferSize <=
            std::numeric_limits<size_t>::max() &&
        frame.UploadArena != nullptr &&
        (frame.UploadArena->Matches(
             desc.UniformBufferSize, desc.VertexBufferSize) ||
         frame.Used == 0U) &&
        frame.UploadArena->Configure(
            *core, *device, desc.UniformBufferSize,
            desc.VertexBufferSize)) {
        std::array<PicaNriUploadRange, 4> uniformRanges{};
        for (size_t index = 0; index < uniformRanges.size(); ++index) {
            uniformRanges[index] = {
                desc.Uniforms[index].Offset,
                desc.Uniforms[index].Size};
        }
        std::vector<PicaNriUploadRange> vertexRanges;
        vertexRanges.reserve(
            desc.VertexBindings.size() + (desc.Indexed ? 1U : 0U));
        for (const auto& binding : desc.VertexBindings)
            vertexRanges.push_back({binding.Offset, binding.Size});
        if (desc.Indexed) {
            vertexRanges.push_back({
                desc.IndexOffset,
                static_cast<uint64_t>(desc.VertexOrIndexCount) *
                    sizeof(uint16_t)});
        }
        uint64_t copiedBytes = 0;
        if (!frame.UploadArena->Copy(
                std::span<const uint8_t>(
                    desc.UniformMappedMemory,
                    static_cast<size_t>(desc.UniformBufferSize)),
                uniformRanges,
                std::span<const uint8_t>(
                    desc.VertexMappedMemory,
                    static_cast<size_t>(desc.VertexBufferSize)),
                vertexRanges, copiedBytes))
            return false;
        uniformBuffer = frame.UploadArena->UniformBuffer();
        vertexBuffer = frame.UploadArena->VertexBuffer();
        mImpl->LastDrawUploadsOwned = true;
        mImpl->LastDrawUploadBytes = copiedBytes;
    }
    if (uniformBuffer == nullptr || vertexBuffer == nullptr) {
        if (!mImpl->Interop->WrapBuffer(
                desc.UniformBuffer, desc.UniformBufferSize,
                desc.UniformMappedMemory) ||
            !mImpl->Interop->WrapBuffer(
                desc.VertexBuffer, desc.VertexBufferSize,
                desc.VertexMappedMemory))
            return false;
        uniformBuffer = mImpl->Interop->Buffer(desc.UniformBuffer);
        vertexBuffer = mImpl->Interop->Buffer(desc.VertexBuffer);
        if (uniformBuffer == nullptr || vertexBuffer == nullptr)
            return false;
    }

    std::array<nri::Descriptor*, 4> constantBuffers{};
    for (size_t index = 0; index < constantBuffers.size(); ++index) {
        const auto& source = desc.Uniforms[index];
        if (source.Size == 0U ||
            source.Offset + source.Size > desc.UniformBufferSize)
            return false;
        nri::BufferViewDesc view{};
        view.buffer = uniformBuffer;
        view.type = nri::BufferView::CONSTANT_BUFFER;
        view.offset = source.Offset;
        view.size = source.Size;
        if (core->CreateBufferView(
                view, constantBuffers[index]) !=
                nri::Result::SUCCESS)
            return false;
        frame.TransientViews.push_back(constantBuffers[index]);
    }

    std::array<NriPicaTextureBindingDesc, 5> textureSources{};
    std::copy(desc.Textures.begin(), desc.Textures.end(),
              textureSources.begin());
    textureSources[3] = desc.DirectionalShadowTexture;
    textureSources[4] = desc.LightingLutTexture;
    std::array<nri::Descriptor*, 5> textures{};
    std::array<nri::Descriptor*, 5> samplers{};
    for (size_t index = 0; index < textures.size(); ++index) {
        const auto& source = textureSources[index];
        if (source.Image == VK_NULL_HANDLE ||
            source.Format == VK_FORMAT_UNDEFINED ||
            source.Width == 0U || source.Height == 0U ||
            source.MipLevels == 0U ||
            !mImpl->Interop->WrapTexture(
                source.Image, source.Format, VK_IMAGE_TYPE_2D,
                source.Usage, source.Width, source.Height,
                source.MipLevels))
            return false;
        textures[index] =
            mImpl->Interop->TextureView(source.Image, false);
        if (textures[index] == nullptr)
            return false;
        const NriPicaSamplerKey key{
            source.MinFilter, source.MagFilter, source.MipmapMode,
            source.AddressU, source.AddressV, source.MipBias,
            source.MinLod, source.MaxLod, source.Integer};
        auto [sampler, inserted] =
            mImpl->Samplers.try_emplace(key, nullptr);
        if (inserted) {
            nri::SamplerDesc samplerDesc{};
            samplerDesc.filters = {
                ToNriFilter(source.MinFilter),
                ToNriFilter(source.MagFilter),
                source.MipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR
                    ? nri::Filter::LINEAR
                    : nri::Filter::NEAREST};
            samplerDesc.mipBias = source.MipBias;
            samplerDesc.mipMin = source.MinLod;
            samplerDesc.mipMax = source.MaxLod;
            samplerDesc.addressModes = {
                ToNriAddressMode(source.AddressU),
                ToNriAddressMode(source.AddressV),
                nri::AddressMode::REPEAT};
            samplerDesc.isInteger = source.Integer;
            if (core->CreateSampler(
                    *mImpl->Interop->Device(),
                    samplerDesc, sampler->second) !=
                    nri::Result::SUCCESS) {
                mImpl->Samplers.erase(sampler);
                return false;
            }
        }
        samplers[index] = sampler->second;
    }

    if (!mImpl->Interop->WrapTexture(
            desc.StorageImage, desc.StorageFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            desc.StorageWidth, desc.StorageHeight, 1U))
        return false;
    nri::Descriptor* storage =
        mImpl->Interop->TextureView(desc.StorageImage, true);
    if (storage == nullptr)
        return false;

    nri::DescriptorSet* set = frame.Sets[frame.Used++];
    const std::array<nri::UpdateDescriptorRangeDesc, 15> updates{{
        {set, 0, 0, &constantBuffers[0], 1},
        {set, 1, 0, &textures[0], 1},
        {set, 2, 0, &textures[1], 1},
        {set, 3, 0, &textures[2], 1},
        {set, 4, 0, &constantBuffers[1], 1},
        {set, 5, 0, &storage, 1},
        {set, 6, 0, &constantBuffers[2], 1},
        {set, 7, 0, &samplers[0], 1},
        {set, 8, 0, &samplers[1], 1},
        {set, 9, 0, &samplers[2], 1},
        {set, 10, 0, &textures[3], 1},
        {set, 11, 0, &samplers[3], 1},
        {set, 12, 0, &constantBuffers[3], 1},
        {set, 13, 0, &textures[4], 1},
        {set, 14, 0, &samplers[4], 1},
    }};
    core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    core->CmdSetDescriptorPool(*command, *frame.Pool);
    core->CmdSetPipelineLayout(
        *command, nri::BindPoint::GRAPHICS,
        *mImpl->DescriptorLayout);
    core->CmdSetPipeline(*command, *pipeline->second);
    const nri::SetDescriptorSetDesc setDesc{
        0, set, nri::BindPoint::GRAPHICS};
    core->CmdSetDescriptorSet(*command, setDesc);
    const nri::SetRootConstantsDesc root{
        0, desc.RootConstants.data(),
        static_cast<uint32_t>(
            desc.RootConstants.size() * sizeof(float))};
    core->CmdSetRootConstants(*command, root);
    for (const auto& binding : desc.VertexBindings) {
        if (binding.Size == 0U || binding.Stride == 0U ||
            binding.Offset > desc.VertexBufferSize ||
            binding.Size >
                desc.VertexBufferSize - binding.Offset)
            return false;
        const nri::VertexBufferDesc vertex{
            vertexBuffer, binding.Offset, binding.Stride};
        core->CmdSetVertexBuffers(
            *command, binding.Binding, &vertex, 1);
    }
    const nri::Viewport viewport{
        desc.Viewport.x, desc.Viewport.y,
        desc.Viewport.width, desc.Viewport.height,
        desc.Viewport.minDepth, desc.Viewport.maxDepth, true};
    const nri::Rect scissor{
        static_cast<int16_t>(std::clamp(
            desc.Scissor.offset.x,
            static_cast<int32_t>(
                std::numeric_limits<int16_t>::min()),
            static_cast<int32_t>(
                std::numeric_limits<int16_t>::max()))),
        static_cast<int16_t>(std::clamp(
            desc.Scissor.offset.y,
            static_cast<int32_t>(
                std::numeric_limits<int16_t>::min()),
            static_cast<int32_t>(
                std::numeric_limits<int16_t>::max()))),
        static_cast<nri::Dim_t>(desc.Scissor.extent.width),
        static_cast<nri::Dim_t>(desc.Scissor.extent.height)};
    core->CmdSetViewports(*command, &viewport, 1);
    core->CmdSetScissors(*command, &scissor, 1);
    const auto usesBlendConstants =
        mImpl->OwnedPipelineUsesBlendConstants.find(
            desc.FallbackPipeline);
    if (usesBlendConstants !=
            mImpl->OwnedPipelineUsesBlendConstants.end() &&
        usesBlendConstants->second) {
        const nri::Color32f blend{
            desc.BlendConstants[0], desc.BlendConstants[1],
            desc.BlendConstants[2], desc.BlendConstants[3]};
        core->CmdSetBlendConstants(*command, blend);
    }
    if (desc.StencilTest) {
        core->CmdSetStencilReference(
            *command, desc.StencilReference,
            desc.StencilReference);
    }
    if (desc.Indexed) {
        const uint64_t indexBytes =
            static_cast<uint64_t>(desc.VertexOrIndexCount) *
            sizeof(uint16_t);
        if (desc.IndexOffset > desc.VertexBufferSize ||
            indexBytes >
                desc.VertexBufferSize - desc.IndexOffset)
            return false;
        core->CmdSetIndexBuffer(
            *command, *vertexBuffer, desc.IndexOffset,
            nri::IndexType::UINT16);
        const nri::DrawIndexedDesc draw{
            desc.VertexOrIndexCount, 1U, 0U,
            desc.BaseVertex, 0U};
        core->CmdDrawIndexed(*command, draw);
    } else {
        const nri::DrawDesc draw{
            desc.VertexOrIndexCount, 1U, 0U, 0U};
        core->CmdDraw(*command, draw);
    }
    return true;
#endif
}

bool NriPicaPipelineBridge::DrawBoundGeometry(const NriPicaOwnedDrawDesc& desc) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)desc;
    return false;
#else
    if (!OwnedDrawsEnabled()) return false;
    const auto pipeline = mImpl->OwnedPipelines.find(desc.FallbackPipeline);
    if (pipeline == mImpl->OwnedPipelines.end()) return false;
    auto* core = mImpl->Interop->Core();
    auto* command = mImpl->Interop->CommandBuffer(desc.FrameIndex);
    if (!core || !command) return false;
    core->CmdSetPipeline(*command, *pipeline->second);
    if (desc.Indexed) {
        core->CmdDrawIndexed(*command, {desc.VertexOrIndexCount, 1U, 0U, desc.BaseVertex, 0U});
    } else {
        core->CmdDraw(*command, {desc.VertexOrIndexCount, 1U, 0U, 0U});
    }
    return true;
#endif
}

bool NriPicaPipelineBridge::Bind(
    uint32_t frameIndex, VkPipeline pipeline) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)frameIndex;
    (void)pipeline;
    return false;
#else
    if (!Available() || pipeline == VK_NULL_HANDLE) return false;
    auto [it, inserted] =
        mImpl->Pipelines.try_emplace(pipeline, nullptr);
    if (inserted) {
        it->second = mImpl->Interop->WrapGraphicsPipeline(pipeline);
        if (it->second == nullptr) {
            mImpl->Pipelines.erase(it);
            return false;
        }
    }
    return mImpl->Interop->CmdSetPipeline(frameIndex, it->second);
#endif
}

void NriPicaPipelineBridge::Forget(VkPipeline pipeline) {
#ifdef ENABLE_RENDERER3DS_NRI
    if (const auto owned = mImpl->OwnedPipelines.find(pipeline);
        owned != mImpl->OwnedPipelines.end()) {
        mImpl->Interop->Core()->DestroyPipeline(owned->second);
        mImpl->OwnedPipelines.erase(owned);
        mImpl->OwnedPipelineUsesBlendConstants.erase(pipeline);
    }
    const auto found = mImpl->Pipelines.find(pipeline);
    if (found == mImpl->Pipelines.end()) return;
    mImpl->Interop->DestroyPipelineWrapper(found->second);
    mImpl->Pipelines.erase(found);
#else
    (void)pipeline;
#endif
}

void NriPicaPipelineBridge::Reset() {
#ifdef ENABLE_RENDERER3DS_NRI
    if (mImpl->Interop != nullptr) {
        for (auto& [_, pipeline] : mImpl->Pipelines)
            mImpl->Interop->DestroyPipelineWrapper(pipeline);
        for (auto& [_, pipeline] : mImpl->OwnedPipelines)
            mImpl->Interop->Core()->DestroyPipeline(pipeline);
    }
    mImpl->Pipelines.clear();
    mImpl->OwnedPipelines.clear();
    mImpl->OwnedPipelineUsesBlendConstants.clear();
#endif
}

void NriPicaPipelineBridge::Shutdown() {
    Reset();
#ifdef ENABLE_RENDERER3DS_NRI
    if (mImpl->Interop != nullptr) {
        nri::CoreInterface* core = mImpl->Interop->Core();
        for (auto& frame : mImpl->Frames) {
            for (nri::Descriptor* view : frame.TransientViews)
                core->DestroyDescriptor(view);
            frame.UploadArena.reset();
            if (frame.Pool != nullptr)
                core->DestroyDescriptorPool(frame.Pool);
        }
        for (auto& [_, sampler] : mImpl->Samplers)
            core->DestroyDescriptor(sampler);
        if (mImpl->DescriptorLayout != nullptr)
            core->DestroyPipelineLayout(mImpl->DescriptorLayout);
        mImpl->DescriptorLayout = nullptr;
        if (mImpl->PipelineCache != nullptr)
            core->DestroyPipelineCache(mImpl->PipelineCache);
        mImpl->PipelineCache = nullptr;
    }
#endif
    *mImpl = {};
    mImpl->Reason = "NRI PICA pipeline bridge is not initialized";
}

bool NriPicaPipelineBridge::Available() const {
    return mImpl->Interop != nullptr && mImpl->Reason.empty() &&
           DescriptorLayoutOwnedByNri();
}
bool NriPicaPipelineBridge::DescriptorLayoutOwnedByNri() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->DescriptorLayout != nullptr;
#else
    return false;
#endif
}
bool NriPicaPipelineBridge::DescriptorsOwnedByNri() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return !mImpl->Frames.empty() &&
           std::all_of(
               mImpl->Frames.begin(), mImpl->Frames.end(),
               [](const Impl::FrameDescriptors& frame) {
                   return frame.Pool != nullptr;
               });
#else
    return false;
#endif
}
bool NriPicaPipelineBridge::OwnedDrawsEnabled() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->OwnedDraws && DescriptorsOwnedByNri();
#else
    return false;
#endif
}
bool NriPicaPipelineBridge::LastDrawUploadsOwnedByNri() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->LastDrawUploadsOwned;
#else
    return false;
#endif
}
uint64_t NriPicaPipelineBridge::LastDrawUploadedBytes() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->LastDrawUploadBytes;
#else
    return 0U;
#endif
}
bool NriPicaPipelineBridge::OwnedPipelineReady(
    VkPipeline fallbackPipeline) const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->OwnedPipelines.contains(fallbackPipeline);
#else
    (void)fallbackPipeline;
    return false;
#endif
}
size_t NriPicaPipelineBridge::OwnedPipelineCount() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->OwnedPipelines.size();
#else
    return 0U;
#endif
}
size_t NriPicaPipelineBridge::WrappedPipelineCount() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->Pipelines.size();
#else
    return 0U;
#endif
}
const std::string&
NriPicaPipelineBridge::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Renderer3ds

#endif
