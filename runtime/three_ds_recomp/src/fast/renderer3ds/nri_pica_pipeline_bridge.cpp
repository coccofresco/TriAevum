#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"
#include <chrono>

#ifdef ENABLE_RENDERER3DS_VULKAN

#include "fast/renderer3ds/pica_nri_shader_contract.h"
#include "fast/renderer3ds/nri_pica_pipeline_identity.h"
#include "fast/renderer3ds/pica_device_pipeline_pool.h"
#include "fast/renderer3ds/pica_nri_upload.h"
#include "fast/renderer3ds/pica_preparation_queue.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"

#ifdef ENABLE_RENDERER3DS_NRI
#include "nri_pica_upload_arena.h"
#include <Extensions/NRIHelper.h>
#include <NRI.h>
#if !defined(TRIAEVUM_NRI_GPL_ABI) || TRIAEVUM_NRI_GPL_ABI != 1
#error "Apply the pinned TriAevum NRI graphics pipeline library ABI patch before building"
#endif
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace Fast::Renderer3ds {
namespace {

// Image/sampler pairs a PICA draw binds: the native units and the extension
// samplers (shadow atlases, contact shadows).
constexpr size_t kTextureSlotCount = 11U;

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
    // Preparation threads update statistics and the lazily created cache.
    mutable std::mutex StatisticsMutex;
    std::mutex PipelineCacheMutex;
    NriPicaPipelineStatistics Statistics;
    NriPicaInterop* Interop = nullptr;
    std::string Reason = "NRI PICA pipeline bridge is not initialized";
#ifdef ENABLE_RENDERER3DS_NRI
    std::unordered_map<VkPipeline, nri::Pipeline*> Pipelines;
    std::map<NriPicaPipelineId, std::shared_ptr<nri::Pipeline>> OwnedPipelines;
    PicaDevicePipelinePool<nri::Pipeline> OwnedPipelineObjects;
    NriPicaPipelineIdentity OwnedPipelineIdentity;
    std::map<NriPicaPipelineId, bool> OwnedPipelineUsesBlendConstants;
    // A background creation owns a copy of its description; ids that asked
    // for an equal pipeline meanwhile adopt the same object.
    struct OwnedCreation {
        NriPicaGraphicsPipelineDesc Desc;
        std::vector<uint32_t> Vertex;
        std::vector<uint32_t> Fragment;
        std::vector<VkVertexInputBindingDescription> Bindings;
        std::vector<VkVertexInputAttributeDescription> Attributes;
        NriPicaPipelineFactory Factory = NriPicaPipelineFactory::Default;
        std::vector<uint8_t> Key;
        std::vector<NriPicaPipelineId> Ids;
        nri::Pipeline* Result = nullptr;
        std::shared_ptr<PicaPreparationTask> Task;
        PicaPreparationQueue* Queue = nullptr;
    };
    std::map<std::vector<uint8_t>, std::shared_ptr<OwnedCreation>> PendingCreations;
    std::map<NriPicaPipelineId, std::shared_ptr<OwnedCreation>> PendingOwned;
    std::set<NriPicaPipelineId> FailedOwned;
    nri::PipelineLayout* DescriptorLayout = nullptr;
    nri::PipelineCache* PipelineCache = nullptr;
    bool PipelineCacheAttempted = false;
    struct DescriptorPoolBlock {
        nri::DescriptorPool* Pool = nullptr;
        std::vector<nri::DescriptorSet*> Sets;
    };
    struct FrameDescriptors {
        std::vector<DescriptorPoolBlock> Pools;
        std::vector<nri::Descriptor*> TransientViews;
        std::unique_ptr<NriPicaUploadArena> UploadArena;
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        size_t Used = 0;
        // The seam binding of draws without seam data (one of TransientViews).
        nri::Descriptor* SeamFallback = nullptr;
        VkBuffer SeamFallbackBuffer = VK_NULL_HANDLE;
    };
    std::vector<FrameDescriptors> Frames;
    std::map<NriPicaSamplerKey, nri::Descriptor*> Samplers;
    // Last sampler per texture slot in BindOwnedDraw.
    std::array<std::pair<NriPicaSamplerKey, nri::Descriptor*>, kTextureSlotCount> RecentSamplers{};
    bool OwnedDraws = false;
    bool PreferOwnedUploads = false;
    bool PipelineLibrariesEnabled = false;
    bool PipelineLibrariesAvailable = false;
    bool LastDrawUploadsOwned = false;
    uint64_t LastDrawUploadBytes = 0;
    uint32_t DescriptorSetsPerPool = 0;

    bool AddDescriptorPool(FrameDescriptors& frame, nri::CoreInterface& core,
                           nri::Device& device) {
        frame.Pools.emplace_back();
        auto& block = frame.Pools.back();
        block.Sets.resize(DescriptorSetsPerPool);
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = DescriptorSetsPerPool;
        pool.constantBufferMaxNum =
            static_cast<uint32_t>(std::tuple_size_v<decltype(NriPicaOwnedDrawDesc::Uniforms)>) *
            DescriptorSetsPerPool;
        pool.textureMaxNum = static_cast<uint32_t>(kTextureSlotCount) * DescriptorSetsPerPool;
        pool.storageTextureMaxNum = DescriptorSetsPerPool;
        pool.samplerMaxNum = static_cast<uint32_t>(kTextureSlotCount) * DescriptorSetsPerPool;
        pool.structuredBufferMaxNum = DescriptorSetsPerPool;
        if (core.CreateDescriptorPool(device, pool, block.Pool) != nri::Result::SUCCESS ||
            core.AllocateDescriptorSets(*block.Pool, *DescriptorLayout, 0,
                block.Sets.data(), DescriptorSetsPerPool, 0) != nri::Result::SUCCESS) {
            if (block.Pool != nullptr) core.DestroyDescriptorPool(block.Pool);
            frame.Pools.pop_back();
            return false;
        }
        return true;
    }
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
        config.DescriptorSetsPerPool == 0U ||
        config.DescriptorSetsPerPool > std::numeric_limits<uint32_t>::max() / 9U) {
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
            case PicaNriDescriptorKind::StructuredBuffer:
                return nri::DescriptorType::STRUCTURED_BUFFER;
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
    mImpl->PipelineLibrariesEnabled = config.PipelineLibrariesEnabled;
    mImpl->PipelineLibrariesAvailable =
        config.PipelineLibrariesEnabled || config.PipelineLibrariesAvailable;
    mImpl->DescriptorSetsPerPool = config.DescriptorSetsPerPool;
    if (mImpl->OwnedDraws) {
        for (auto& frame : mImpl->Frames) {
            if (!mImpl->AddDescriptorPool(frame, *core, *device)) {
                mImpl->OwnedDraws = false;
                break;
            }
        }
        if (!mImpl->OwnedDraws) {
            for (auto& frame : mImpl->Frames) {
                for (const auto& block : frame.Pools)
                    core->DestroyDescriptorPool(block.Pool);
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
    std::lock_guard lock(mImpl->PipelineCacheMutex);
    if (mImpl->PipelineCacheAttempted) return mImpl->PipelineCache != nullptr;
    mImpl->PipelineCacheAttempted = true;
    nri::PipelineCacheDesc desc{};
    desc.data = data.data();
    desc.size = data.size();
    auto* core = mImpl->Interop->Core();
    auto result = core->CreatePipelineCache(*mImpl->Interop->Device(), desc, mImpl->PipelineCache);
    if (result == nri::Result::SUCCESS && mImpl->PipelineCache != nullptr) {
        std::lock_guard statistics(mImpl->StatisticsMutex);
        mImpl->Statistics.InitialCacheBytes = data.size();
    }
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
    // No size policy here: the store keeps or drops the cache (it outgrows the
    // plain 64 MiB limit; capping it here once left it silently unsaved).
    // Only an implausible size is refused before allocating it.
    constexpr uint64_t kImplausibleBytes = 4ULL * kMaximumNriPipelineCacheBytes;
    if (core->GetPipelineCacheData(*mImpl->PipelineCache, nullptr, size) != nri::Result::SUCCESS ||
        size == 0 || size > kImplausibleBytes) return {};
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (core->GetPipelineCacheData(*mImpl->PipelineCache, data.data(), size) != nri::Result::SUCCESS ||
        size == 0 || size > data.size()) return {};
    data.resize(static_cast<size_t>(size));
    return data;
#endif
}

uint64_t NriPicaTraceProgramHash(std::span<const uint32_t> words) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint32_t word : words)
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= (word >> shift) & 0xffU;
            hash *= 1099511628211ULL;
        }
    return hash;
}

bool NriPicaPipelineTraceEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_NRI_PIPELINE_TRACE");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

#ifdef ENABLE_RENDERER3DS_NRI
namespace {
// TRIAEVUM_NRI_PIPELINE_TRACE=1: one line per created pipeline with the full
// creation input (programs by content hash), so a prepared cache can be
// compared field by field with what a game session creates.
void TracePipelineCreation(const NriPicaGraphicsPipelineDesc& desc, bool libraries, long long nanoseconds) {
    if (!NriPicaPipelineTraceEnabled())
        return;
    std::string line = "TRIAEVUM_NRI_PIPELINE_CREATE";
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), " vs=%016llx fs=%016llx libraries=%u ms=%.3f",
                  static_cast<unsigned long long>(NriPicaTraceProgramHash(desc.VertexSpirv)),
                  static_cast<unsigned long long>(NriPicaTraceProgramHash(desc.FragmentSpirv)), libraries ? 1U : 0U,
                  static_cast<double>(nanoseconds) / 1.0e6);
    line += buffer;
    std::snprintf(buffer, sizeof(buffer),
                  " topo=%u cull=%u front=%u samples=%u a2c=%u depth=%u/%u/%u stencil=%u logic=%u/%u ds=%u colors=%u",
                  unsigned(desc.Topology), unsigned(desc.CullMode), unsigned(desc.FrontFace), unsigned(desc.Samples),
                  unsigned(desc.AlphaToCoverage), unsigned(desc.DepthTest), unsigned(desc.DepthWrite),
                  unsigned(desc.DepthCompare), unsigned(desc.StencilTest), unsigned(desc.LogicOpEnabled),
                  unsigned(desc.LogicOp), unsigned(desc.DepthStencilFormat), desc.ColorAttachmentCount);
    line += buffer;
    if (desc.StencilTest) {
        for (const auto* face : { &desc.FrontStencil, &desc.BackStencil }) {
            std::snprintf(buffer, sizeof(buffer), " st=%u/%u/%u/%u/%x/%x", unsigned(face->failOp),
                          unsigned(face->passOp), unsigned(face->depthFailOp), unsigned(face->compareOp),
                          face->compareMask, face->writeMask);
            line += buffer;
        }
    }
    for (uint32_t index = 0; index < desc.ColorAttachmentCount && index < desc.Colors.size(); ++index) {
        const auto& color = desc.Colors[index];
        std::snprintf(buffer, sizeof(buffer), " c%u=%u:%u:%u/%u/%u/%u/%u/%u:%x", index,
                      unsigned(desc.ColorFormats[index]), unsigned(color.blendEnable),
                      unsigned(color.srcColorBlendFactor), unsigned(color.dstColorBlendFactor),
                      unsigned(color.colorBlendOp), unsigned(color.srcAlphaBlendFactor),
                      unsigned(color.dstAlphaBlendFactor), unsigned(color.alphaBlendOp), color.colorWriteMask);
        line += buffer;
    }
    for (const auto& binding : desc.VertexBindings) {
        std::snprintf(buffer, sizeof(buffer), " b%u=%u/%u", binding.binding, binding.stride,
                      unsigned(binding.inputRate));
        line += buffer;
    }
    for (const auto& attribute : desc.VertexAttributes) {
        std::snprintf(buffer, sizeof(buffer), " a%u=%u/%u/%u", attribute.location, attribute.binding,
                      unsigned(attribute.format), attribute.offset);
        line += buffer;
    }
    line += '\n';
    std::fputs(line.c_str(), stderr);
}
} // namespace

nri::PipelineCache* NriPicaPipelineBridge::PipelineCache() const {
    std::lock_guard lock(mImpl->PipelineCacheMutex);
    return mImpl->PipelineCache;
}

nri::Pipeline* NriPicaPipelineBridge::CreatePipeline(const NriPicaGraphicsPipelineDesc& desc,
                                                     NriPicaPipelineFactory factory) {
    if (!Available() || desc.VertexSpirv.empty() || desc.FragmentSpirv.empty() ||
        desc.ColorAttachmentCount == 0U || desc.ColorAttachmentCount > kPicaColorAttachmentCount ||
        desc.VertexBindings.size() > std::numeric_limits<uint8_t>::max() ||
        desc.VertexAttributes.size() > std::numeric_limits<uint8_t>::max())
        return nullptr;
    const nri::Topology topology = ToNriTopology(desc.Topology);
    const nri::CullMode cullMode = ToNriCullMode(desc.CullMode);
    if (topology == nri::Topology::MAX_NUM ||
        cullMode == nri::CullMode::MAX_NUM)
        return nullptr;

    std::vector<nri::VertexStreamDesc> streams;
    streams.reserve(desc.VertexBindings.size());
    for (const auto& binding : desc.VertexBindings) {
        if (binding.binding >
                std::numeric_limits<uint16_t>::max() ||
            binding.stride >
                std::numeric_limits<uint16_t>::max())
            return nullptr;
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
            return nullptr;
        nri::VertexAttributeDesc converted{};
        converted.vk.location = attribute.location;
        converted.offset = attribute.offset;
        converted.format =
            nri::nriConvertVKFormatToNRI(attribute.format);
        converted.streamIndex =
            static_cast<uint16_t>(attribute.binding);
        if (converted.format == nri::Format::UNKNOWN)
            return nullptr;
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
            return nullptr;
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
        return nullptr;

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
    const bool libraries = factory == NriPicaPipelineFactory::Libraries
        ? mImpl->PipelineLibrariesAvailable
        : factory == NriPicaPipelineFactory::Default && mImpl->PipelineLibrariesEnabled;
    if (libraries)
        pipeline.flags = nri::GraphicsPipelineBits::USE_GRAPHICS_PIPELINE_LIBRARY;
    // Reuse driver compilation across layout/raster variants and Reset().
    // Cache failure is non-fatal: the same canonical pipeline remains valid.
    InitializePipelineCache();
    pipeline.cache = mImpl->PipelineCache;
    nri::Pipeline* owned = nullptr;
    const auto start = std::chrono::steady_clock::now();
    const auto result = mImpl->Interop->Core()->CreateGraphicsPipeline(
        *mImpl->Interop->Device(), pipeline, owned);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    TracePipelineCreation(desc, libraries, elapsed);
    std::lock_guard statistics(mImpl->StatisticsMutex);
    ++mImpl->Statistics.CreationAttempts;
    mImpl->Statistics.CreationNanoseconds += elapsed;
    if (result != nri::Result::SUCCESS || owned == nullptr)
        return nullptr;
    ++mImpl->Statistics.Created;
    return owned;
}
#endif

bool NriPicaPipelineBridge::PreparePipeline(const NriPicaGraphicsPipelineDesc& desc,
                                            NriPicaPipelineFactory factory) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)desc;
    (void)factory;
    return false;
#else
    auto* pipeline = CreatePipeline(desc, factory);
    if (!pipeline) return false;
    mImpl->Interop->Core()->DestroyPipeline(pipeline);
    return true;
#endif
}

NriPicaPipelineStatistics NriPicaPipelineBridge::PipelineStatistics() const {
    std::lock_guard statistics(mImpl->StatisticsMutex);
    return mImpl->Statistics;
}

#ifdef ENABLE_RENDERER3DS_NRI
namespace {
bool UsesBlendConstants(const NriPicaGraphicsPipelineDesc& desc) {
    return std::any_of(desc.Colors.begin(), desc.Colors.end(),
        [](const VkPipelineColorBlendAttachmentState& color) {
            return ReferencesBlendConstants(color.srcColorBlendFactor) ||
                   ReferencesBlendConstants(color.dstColorBlendFactor) ||
                   ReferencesBlendConstants(color.srcAlphaBlendFactor) ||
                   ReferencesBlendConstants(color.dstAlphaBlendFactor);
        });
}

bool ValidOwnedDesc(const NriPicaGraphicsPipelineDesc& desc) {
    return !desc.VertexSpirv.empty() && !desc.FragmentSpirv.empty() &&
           desc.ColorAttachmentCount != 0 && desc.ColorAttachmentCount <= kPicaColorAttachmentCount;
}
} // namespace

void NriPicaPipelineBridge::AdoptOwnedPipeline(
    NriPicaPipelineId pipelineId, std::shared_ptr<nri::Pipeline> owned,
    const NriPicaGraphicsPipelineDesc& desc) {
    mImpl->OwnedPipelines.emplace(pipelineId, std::move(owned));
    mImpl->OwnedPipelineUsesBlendConstants.emplace(pipelineId, UsesBlendConstants(desc));
}
#endif

NriPicaPipelineRequest NriPicaPipelineBridge::RequestOwnedPipeline(
    NriPicaPipelineId pipelineId, const NriPicaGraphicsPipelineDesc& desc,
    NriPicaPipelineFactory factory, PicaPreparationQueue& queue) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)pipelineId;
    (void)desc;
    (void)factory;
    (void)queue;
    return NriPicaPipelineRequest::Failed;
#else
    if (mImpl->OwnedPipelines.contains(pipelineId)) return NriPicaPipelineRequest::Ready;
    if (mImpl->FailedOwned.contains(pipelineId)) return NriPicaPipelineRequest::Failed;
    if (mImpl->PendingOwned.contains(pipelineId)) return PollOwnedPipeline(pipelineId);
    if (!pipelineId || !Available() || !ValidOwnedDesc(desc)) {
        mImpl->FailedOwned.insert(pipelineId);
        return NriPicaPipelineRequest::Failed;
    }
    auto key = mImpl->OwnedPipelineIdentity.Build(desc);
    if (auto owned = mImpl->OwnedPipelineObjects.Find(key)) {
        std::lock_guard statistics(mImpl->StatisticsMutex);
        ++mImpl->Statistics.OwnedReuses;
        AdoptOwnedPipeline(pipelineId, std::move(owned), desc);
        return NriPicaPipelineRequest::Ready;
    }
    if (const auto pending = mImpl->PendingCreations.find(key); pending != mImpl->PendingCreations.end()) {
        pending->second->Ids.push_back(pipelineId);
        mImpl->PendingOwned.emplace(pipelineId, pending->second);
        return PollOwnedPipeline(pipelineId);
    }
    auto creation = std::make_shared<Impl::OwnedCreation>();
    creation->Desc = desc;
    creation->Vertex.assign(desc.VertexSpirv.begin(), desc.VertexSpirv.end());
    creation->Fragment.assign(desc.FragmentSpirv.begin(), desc.FragmentSpirv.end());
    creation->Bindings.assign(desc.VertexBindings.begin(), desc.VertexBindings.end());
    creation->Attributes.assign(desc.VertexAttributes.begin(), desc.VertexAttributes.end());
    creation->Desc.VertexSpirv = creation->Vertex;
    creation->Desc.FragmentSpirv = creation->Fragment;
    creation->Desc.VertexBindings = creation->Bindings;
    creation->Desc.VertexAttributes = creation->Attributes;
    creation->Factory = factory;
    creation->Key = key;
    creation->Ids.push_back(pipelineId);
    creation->Queue = &queue;
    {
        std::lock_guard statistics(mImpl->StatisticsMutex);
        ++mImpl->Statistics.BackgroundRequests;
    }
    // Reset() completes every pending creation before the bridge state goes.
    creation->Task = queue.Submit([this, raw = creation.get()] {
        raw->Result = CreatePipeline(raw->Desc, raw->Factory);
    });
    mImpl->PendingCreations.emplace(std::move(key), creation);
    mImpl->PendingOwned.emplace(pipelineId, std::move(creation));
    return PollOwnedPipeline(pipelineId);
#endif
}

NriPicaPipelineRequest NriPicaPipelineBridge::PollOwnedPipeline(NriPicaPipelineId pipelineId) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)pipelineId;
    return NriPicaPipelineRequest::Failed;
#else
    if (mImpl->OwnedPipelines.contains(pipelineId)) return NriPicaPipelineRequest::Ready;
    const auto pending = mImpl->PendingOwned.find(pipelineId);
    if (pending == mImpl->PendingOwned.end()) return NriPicaPipelineRequest::Failed;
    const auto creation = pending->second;
    if (!creation->Task->Done()) return NriPicaPipelineRequest::Pending;
    mImpl->PendingCreations.erase(creation->Key);
    if (creation->Result == nullptr) {
        for (const auto id : creation->Ids) {
            mImpl->PendingOwned.erase(id);
            mImpl->FailedOwned.insert(id);
        }
        return NriPicaPipelineRequest::Failed;
    }
    std::shared_ptr<nri::Pipeline> owned(creation->Result,
        [core = mImpl->Interop->Core()](nri::Pipeline* value) { core->DestroyPipeline(value); });
    creation->Result = nullptr;
    {
        std::lock_guard statistics(mImpl->StatisticsMutex);
        ++mImpl->Statistics.BackgroundCreated;
    }
    for (const auto id : creation->Ids) {
        mImpl->PendingOwned.erase(id);
        AdoptOwnedPipeline(id, owned, creation->Desc);
    }
    mImpl->OwnedPipelineObjects.Retain(creation->Key, std::move(owned));
    return mImpl->OwnedPipelines.contains(pipelineId) ? NriPicaPipelineRequest::Ready
                                                      : NriPicaPipelineRequest::Failed;
#endif
}

NriPicaPipelineRequest NriPicaPipelineBridge::CompleteOwnedPipeline(NriPicaPipelineId pipelineId) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)pipelineId;
    return NriPicaPipelineRequest::Failed;
#else
    const auto pending = mImpl->PendingOwned.find(pipelineId);
    if (pending != mImpl->PendingOwned.end() && !pending->second->Task->Done()) {
        {
            std::lock_guard statistics(mImpl->StatisticsMutex);
            ++mImpl->Statistics.BackgroundCompletions;
        }
        const auto creation = pending->second;
        creation->Queue->Complete(*creation->Task);
    }
    return PollOwnedPipeline(pipelineId);
#endif
}

size_t NriPicaPipelineBridge::PendingOwnedPipelineCount() const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->PendingCreations.size();
#else
    return 0U;
#endif
}

#ifdef ENABLE_RENDERER3DS_NRI
void NriPicaPipelineBridge::CompleteAllPendingPipelines() {
    for (const auto& [key, creation] : mImpl->PendingCreations) {
        creation->Queue->Complete(*creation->Task);
        if (creation->Result != nullptr && mImpl->Interop != nullptr)
            mImpl->Interop->Core()->DestroyPipeline(creation->Result);
        creation->Result = nullptr;
    }
    mImpl->PendingCreations.clear();
    mImpl->PendingOwned.clear();
    mImpl->FailedOwned.clear();
}
#endif

bool NriPicaPipelineBridge::CreateOwnedPipeline(
    NriPicaPipelineId pipelineId, const NriPicaGraphicsPipelineDesc& desc,
    NriPicaPipelineFactory factory) {
#ifndef ENABLE_RENDERER3DS_NRI
    (void)pipelineId;
    (void)desc;
    (void)factory;
    return false;
#else
    if (mImpl->PendingOwned.contains(pipelineId))
        return CompleteOwnedPipeline(pipelineId) == NriPicaPipelineRequest::Ready;
    if (!pipelineId || mImpl->OwnedPipelines.contains(pipelineId) || !Available() ||
        !ValidOwnedDesc(desc))
        return false;
    auto key = mImpl->OwnedPipelineIdentity.Build(desc);
    auto owned = mImpl->OwnedPipelineObjects.Find(key);
    if (owned) {
        std::lock_guard statistics(mImpl->StatisticsMutex);
        ++mImpl->Statistics.OwnedReuses;
    } else {
        auto* pipeline = CreatePipeline(desc, factory);
        if (!pipeline) {
            return false;
        }
        owned = std::shared_ptr<nri::Pipeline>(pipeline,
            [core = mImpl->Interop->Core()](nri::Pipeline* value) {
                core->DestroyPipeline(value);
            });
        mImpl->OwnedPipelineObjects.Retain(std::move(key), owned);
    }
    AdoptOwnedPipeline(pipelineId, std::move(owned), desc);
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
        !desc.PipelineId ||
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
        mImpl->OwnedPipelines.find(desc.PipelineId);
    nri::CommandBuffer* command =
        mImpl->Interop->CommandBuffer(desc.FrameIndex);
    auto& frame = mImpl->Frames[
        desc.FrameIndex % mImpl->Frames.size()];
    nri::CoreInterface* core = mImpl->Interop->Core();
    nri::Device* device = mImpl->Interop->Device();
    if (pipeline == mImpl->OwnedPipelines.end() ||
        command == nullptr || core == nullptr || device == nullptr ||
        frame.Pools.empty())
        return false;
    if (frame.FrameId != desc.FrameId) {
        for (nri::Descriptor* view : frame.TransientViews)
            core->DestroyDescriptor(view);
        frame.TransientViews.clear();
        frame.SeamFallback = nullptr;
        frame.SeamFallbackBuffer = VK_NULL_HANDLE;
        frame.FrameId = desc.FrameId;
        frame.Used = 0;
    }
    const size_t poolIndex = frame.Used / mImpl->DescriptorSetsPerPool;
    // Earlier sets are already referenced by recorded commands. Retain their
    // pools and reuse every block only when this frame slot's fence has passed.
    if (poolIndex == frame.Pools.size() &&
        !mImpl->AddDescriptorPool(frame, *core, *device))
        return false;
    auto& descriptorPool = frame.Pools[poolIndex];

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
        std::array<PicaNriUploadRange, kNriPicaUniformBindingCount> uniformRanges{};
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

    std::array<nri::Descriptor*, kNriPicaUniformBindingCount> constantBuffers{};
    for (size_t index = 0; index < constantBuffers.size(); ++index) {
        const auto& source = desc.Uniforms[index];
        if (source.Size == 0U ||
            source.Offset + source.Size > desc.UniformBufferSize)
            return false;
        // Optional, unused bindings may alias an existing uniform range.
        // Reuse its view too; only unique descriptors enter TransientViews.
        for (size_t previous = 0; previous < index; ++previous) {
            if (desc.Uniforms[previous].Offset == source.Offset &&
                desc.Uniforms[previous].Size == source.Size) {
                constantBuffers[index] = constantBuffers[previous];
                break;
            }
        }
        if (constantBuffers[index]) continue;
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

    // Keep the source arena for seam data even when the optional NRI upload
    // arena owns the seven constant ranges. No second copy of geometry metadata.
    nri::Descriptor* seamBuffer = nullptr;
    if (desc.SeamGeometry.Size != 0) {
        if (!mImpl->Interop->WrapBuffer(desc.UniformBuffer, desc.UniformBufferSize, desc.UniformMappedMemory))
            return false;
        const auto seamRange = desc.SeamGeometry;
        if (seamRange.Offset > desc.UniformBufferSize ||
            seamRange.Size > desc.UniformBufferSize - seamRange.Offset)
            return false;
        nri::BufferViewDesc seamView{};
        seamView.buffer = mImpl->Interop->Buffer(desc.UniformBuffer);
        seamView.type = nri::BufferView::STRUCTURED_BUFFER;
        seamView.structureStride = 16;
        seamView.offset = seamRange.Offset;
        seamView.size = seamRange.Size;
        if (core->CreateBufferView(seamView, seamBuffer) != nri::Result::SUCCESS)
            return false;
        frame.TransientViews.push_back(seamBuffer);
    } else {
        // Binding 26 is in every set, and the always-available surface programs
        // declare it (they read it only when the draw's decisions select seam
        // softening). A draw without seam data binds a valid view instead of
        // leaving a statically used descriptor unwritten.
        if (frame.SeamFallback == nullptr || frame.SeamFallbackBuffer != desc.UniformBuffer) {
            if (desc.UniformBufferSize < 16U ||
                !mImpl->Interop->WrapBuffer(desc.UniformBuffer, desc.UniformBufferSize, desc.UniformMappedMemory))
                return false;
            nri::BufferViewDesc fallback{};
            fallback.buffer = mImpl->Interop->Buffer(desc.UniformBuffer);
            fallback.type = nri::BufferView::STRUCTURED_BUFFER;
            fallback.structureStride = 16;
            fallback.offset = 0;
            fallback.size = 16;
            nri::Descriptor* view = nullptr;
            if (core->CreateBufferView(fallback, view) != nri::Result::SUCCESS)
                return false;
            frame.TransientViews.push_back(view);
            frame.SeamFallback = view;
            frame.SeamFallbackBuffer = desc.UniformBuffer;
        }
        seamBuffer = frame.SeamFallback;
    }

    std::array<NriPicaTextureBindingDesc, kTextureSlotCount> textureSources{};
    std::copy(desc.Textures.begin(), desc.Textures.end(),
              textureSources.begin());
    textureSources[3] = desc.DirectionalShadowTexture;
    textureSources[4] = desc.LightingLutTexture;
    std::copy(desc.PbrTextures.begin(), desc.PbrTextures.end(), textureSources.begin() + 5);
    textureSources[9] = desc.PointShadowTexture;
    textureSources[10] = desc.ContactShadowTexture;
    std::array<nri::Descriptor*, kTextureSlotCount> textures{};
    std::array<nri::Descriptor*, kTextureSlotCount> samplers{};
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
        // Consecutive draws mostly repeat each slot's sampler. Equal keys are
        // equivalent under the map's ordering, and map entries live until
        // Shutdown resets the whole Impl, so the remembered descriptor is the
        // one the lookup would return.
        auto& recent = mImpl->RecentSamplers[index];
        if (recent.second != nullptr && recent.first == key) {
            samplers[index] = recent.second;
            continue;
        }
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
        recent = {key, sampler->second};
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

    nri::DescriptorSet* set =
        descriptorPool.Sets[frame.Used++ % mImpl->DescriptorSetsPerPool];
    const std::array<nri::UpdateDescriptorRangeDesc, kPicaNriDescriptorBindings.size()> updates{{
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
        {set, 15, 0, &constantBuffers[4], 1},
        {set, 16, 0, &constantBuffers[5], 1},
        {set, 17, 0, &constantBuffers[6], 1},
        {set, 18, 0, &textures[5], 1},
        {set, 19, 0, &textures[6], 1},
        {set, 20, 0, &textures[7], 1},
        {set, 21, 0, &samplers[5], 1},
        {set, 22, 0, &samplers[6], 1},
        {set, 23, 0, &samplers[7], 1},
        {set, 24, 0, &textures[8], 1},
        {set, 25, 0, &samplers[8], 1},
        {set, 27, 0, &textures[9], 1},
        {set, 28, 0, &samplers[9], 1},
        {set, 29, 0, &textures[10], 1},
        {set, 30, 0, &samplers[10], 1},
        // The draw's seam data, or the frame's fallback view.
        {set, 26, 0, &seamBuffer, 1},
    }};
    core->UpdateDescriptorRanges(updates.data(), static_cast<uint32_t>(updates.size()));
    core->CmdSetDescriptorPool(*command, *descriptorPool.Pool);
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
            desc.PipelineId);
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
    const auto pipeline = mImpl->OwnedPipelines.find(desc.PipelineId);
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

void NriPicaPipelineBridge::ForgetOwned(NriPicaPipelineId pipelineId) {
#ifdef ENABLE_RENDERER3DS_NRI
    mImpl->FailedOwned.erase(pipelineId);
    if (const auto pending = mImpl->PendingOwned.find(pipelineId); pending != mImpl->PendingOwned.end()) {
        // The creation continues for the other ids that asked for it.
        auto& ids = pending->second->Ids;
        ids.erase(std::remove(ids.begin(), ids.end(), pipelineId), ids.end());
        mImpl->PendingOwned.erase(pending);
    }
    if (const auto owned = mImpl->OwnedPipelines.find(pipelineId);
        owned != mImpl->OwnedPipelines.end()) {
        mImpl->OwnedPipelines.erase(owned);
        mImpl->OwnedPipelineUsesBlendConstants.erase(pipelineId);
        mImpl->OwnedPipelineObjects.Prune();
    }
#else
    (void)pipelineId;
#endif
}

void NriPicaPipelineBridge::Forget(VkPipeline pipeline) {
#ifdef ENABLE_RENDERER3DS_NRI
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
    CompleteAllPendingPipelines();
    if (mImpl->Interop != nullptr) {
        for (auto& [_, pipeline] : mImpl->Pipelines)
            mImpl->Interop->DestroyPipelineWrapper(pipeline);
    }
    mImpl->Pipelines.clear();
    mImpl->OwnedPipelines.clear();
    mImpl->OwnedPipelineObjects.Clear();
    mImpl->OwnedPipelineIdentity.Clear();
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
            for (const auto& block : frame.Pools)
                if (block.Pool != nullptr) core->DestroyDescriptorPool(block.Pool);
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
    mImpl = std::make_unique<Impl>();
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
                   return !frame.Pools.empty();
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
    NriPicaPipelineId pipelineId) const {
#ifdef ENABLE_RENDERER3DS_NRI
    return mImpl->OwnedPipelines.contains(pipelineId);
#else
    (void)pipelineId;
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
