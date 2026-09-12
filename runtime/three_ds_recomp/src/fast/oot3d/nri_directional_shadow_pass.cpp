#include "fast/oot3d/nri_directional_shadow_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/oot3d/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#include <NRI.h>
#endif


#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kMaximumCasters = 512U;
constexpr size_t kMaximumShadowTargets = 8U;

uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return ((value + alignment - 1U) / alignment) * alignment;
}

struct alignas(16) ShadowTransformUniforms {
    DirectionalShadowMatrix ViewToWorld{};
    DirectionalShadowMatrix WorldToLightClip{};
};

static_assert(sizeof(ShadowTransformUniforms) == 128U);

struct ShadowTargetKey {
    uint64_t RenderTargetNamespace = 0U;
    uint32_t ColorPhysicalAddress = 0U;

    bool operator==(const ShadowTargetKey&) const = default;
};

struct ShadowTargetKeyHash {
    size_t operator()(const ShadowTargetKey& key) const noexcept {
        uint64_t value = key.RenderTargetNamespace;
        value ^= static_cast<uint64_t>(key.ColorPhysicalAddress) +
                 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        return static_cast<size_t>(value);
    }
};

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders, const std::string& source,
                              Renderer::SpirvStage kind,
                              const char* name) {
    return shaders.Resolve(source, kind, name);
}

uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kPrime;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(value));
}

#ifdef ENABLE_OOT3D_NRI
nri::Topology ToNriTopology(PicaSceneTopology topology) {
    switch (topology) {
        case PicaSceneTopology::TriangleList:
            return nri::Topology::TRIANGLE_LIST;
        case PicaSceneTopology::TriangleStrip:
            return nri::Topology::TRIANGLE_STRIP;
    }
    return nri::Topology::MAX_NUM;
}

nri::CullMode ToNriCullMode(PicaSceneCullMode mode) {
    switch (mode) {
        case PicaSceneCullMode::None:
            return nri::CullMode::NONE;
        case PicaSceneCullMode::Front:
            return nri::CullMode::FRONT;
        case PicaSceneCullMode::Back:
            return nri::CullMode::BACK;
    }
    return nri::CullMode::MAX_NUM;
}

template <typename Handle>
Handle FromSceneNativeHandle(uintptr_t value) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}
#endif

} // namespace

struct NriDirectionalShadowPass::Impl {
    struct HistorySlot {
        NriOwnedTexture2D Texture;
        PicaDirectionalShadowHistoryState State;
    };

    struct TargetHistory {
        std::array<HistorySlot, kFrameSlots> Slots;
        uint32_t Resolution = 0U;
        uint64_t LastUsedFrameId = 0U;
    };

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkSampler ReceiverSampler = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    std::unordered_map<ShadowTargetKey, TargetHistory,
                       ShadowTargetKeyHash>
        Histories;
    std::vector<NriOwnedTexture2D> RetiredTextures;
    ResourceStateTracker OwnedStates;
    bool LastNativeLight = false;
    uint32_t LastCasters = 0U;
    NriDirectionalShadowLightTelemetry LastLight;
    NriDirectionalShadowExecutionStage LastStage =
        NriDirectionalShadowExecutionStage::NotAttempted;
    EffectPassBarrierExecution LastShadowMapBarrierExecution;
    std::string Reason =
        "NRI directional shadow pass is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* ShadowLayout = nullptr;
    struct FrameDescriptors {
        nri::DescriptorPool* CasterPool = nullptr;
        std::vector<nri::DescriptorSet*> CasterSets;
        nri::Buffer* ShadowUniformBuffer = nullptr;
        std::vector<nri::Descriptor*> ShadowUniformViews;
        uint8_t* ShadowUniformMapped = nullptr;
        uint64_t ShadowUniformStride = 0U;
        std::vector<nri::Descriptor*> TransientViews;
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        uint32_t CastersUsed = 0U;
    };
    std::array<FrameDescriptors, kFrameSlots> Frames;
    std::unordered_map<uint64_t, nri::Pipeline*> ShadowPipelines;
    std::unordered_set<uint64_t> FailedShadowPipelines;
#endif

    void Retire(TargetHistory& history) {
        for (auto& slot : history.Slots) {
            if (slot.Texture.Image != VK_NULL_HANDLE) {
                OwnedStates.Forget(
                    reinterpret_cast<uintptr_t>(slot.Texture.Image));
                RetiredTextures.push_back(slot.Texture);
            }
            slot = {};
        }
        history = {};
    }

    void DestroyOwnedTextures() {
        if (Interop != nullptr) {
            for (auto& [key, history] : Histories) {
                (void)key;
                for (auto& slot : history.Slots) {
                    Interop->DestroyOwnedTexture(slot.Texture.Image);
                }
            }
            for (auto& texture : RetiredTextures) {
                Interop->DestroyOwnedTexture(texture.Image);
            }
        }
        Histories.clear();
        RetiredTextures.clear();
        OwnedStates.Clear();
    }

#ifdef ENABLE_OOT3D_NRI
    TargetHistory* EnsureTarget(const ShadowTargetKey& key,
                                uint32_t resolution,
                                uint64_t frameId) {
        auto found = Histories.find(key);
        if (found != Histories.end() &&
            found->second.Resolution == resolution &&
            std::all_of(
                found->second.Slots.begin(), found->second.Slots.end(),
                [](const HistorySlot& slot) {
                    return slot.Texture.Image != VK_NULL_HANDLE;
                })) {
            found->second.LastUsedFrameId = frameId;
            return &found->second;
        }

        TargetHistory replacement;
        replacement.Resolution = resolution;
        replacement.LastUsedFrameId = frameId;
        NriOwnedTexture2DDesc textureDesc;
        textureDesc.Width = resolution;
        textureDesc.Height = resolution;
        textureDesc.Format = VK_FORMAT_D32_SFLOAT;
        textureDesc.Usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        for (auto& slot : replacement.Slots) {
            if (!Interop->CreateOwnedTexture2D(textureDesc, slot.Texture)) {
                for (auto& created : replacement.Slots) {
                    Interop->DestroyOwnedTexture(created.Texture.Image);
                }
                return nullptr;
            }
        }

        if (found != Histories.end()) {
            Retire(found->second);
            found->second = std::move(replacement);
            return &found->second;
        }
        if (Histories.size() >= kMaximumShadowTargets) {
            const auto oldest = std::min_element(
                Histories.begin(), Histories.end(),
                [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrameId <
                           right.second.LastUsedFrameId;
                });
            if (oldest != Histories.end()) {
                Retire(oldest->second);
                Histories.erase(oldest);
            }
        }
        return &Histories.emplace(key, std::move(replacement))
                    .first->second;
    }

    uint64_t PipelineKey(const PicaSceneDrawRecord& caster,
                         const DirectionalShadowFramePlan& plan) const {
        uint64_t hash = 1469598103934665603ULL;
        hash = HashValue(hash, caster.VertexShader->Key);
        hash = HashValue(hash, caster.Material.Topology);
        hash = HashValue(hash, caster.Material.CullMode);
        hash = HashValue(hash, caster.Material.FrontFace);
        hash = HashValue(hash, plan.DepthBiasConstant);
        hash = HashValue(hash, plan.DepthBiasSlope);
        hash = HashValue(hash, caster.VertexLayout->StructuralKey);
        return hash;
    }

    nri::Pipeline* GetOrCreatePipeline(
        const PicaSceneDrawRecord& caster,
        const DirectionalShadowFramePlan& plan) {
        const uint64_t key = PipelineKey(caster, plan);
        const auto found = ShadowPipelines.find(key);
        if (found != ShadowPipelines.end()) {
            return found->second;
        }
        if (FailedShadowPipelines.contains(key)) {
            return nullptr;
        }
        const auto reject = [this, key]() -> nri::Pipeline* {
            FailedShadowPipelines.insert(key);
            return nullptr;
        };
        const nri::Topology topology =
            ToNriTopology(caster.Material.Topology);
        const nri::CullMode cull =
            ToNriCullMode(caster.Material.CullMode);
        if (topology == nri::Topology::MAX_NUM ||
            cull == nri::CullMode::MAX_NUM) {
            return reject();
        }

        std::vector<nri::VertexStreamDesc> streams;
        streams.reserve(caster.VertexLayout->Bindings.size());
        for (const auto& source : caster.VertexLayout->Bindings) {
            if (source.Binding > std::numeric_limits<uint16_t>::max() ||
                source.ByteStride >
                    std::numeric_limits<uint16_t>::max()) {
                return reject();
            }
            streams.push_back({
                static_cast<uint16_t>(source.Binding),
                source.PerInstance
                    ? nri::VertexStreamStepRate::PER_INSTANCE
                    : nri::VertexStreamStepRate::PER_VERTEX,
                static_cast<uint16_t>(source.ByteStride),
            });
        }
        std::vector<nri::VertexAttributeDesc> attributes;
        attributes.reserve(caster.VertexLayout->Attributes.size());
        for (const auto& source : caster.VertexLayout->Attributes) {
            if (source.Location >
                    std::numeric_limits<uint16_t>::max() ||
                source.Binding >
                    std::numeric_limits<uint16_t>::max()) {
                return reject();
            }
            nri::VertexAttributeDesc attribute{};
            attribute.vk.location = source.Location;
            attribute.offset = source.ByteOffset;
            attribute.format = nri::Format::RGBA32_SFLOAT;
            attribute.streamIndex =
                static_cast<uint16_t>(source.Binding);
            attributes.push_back(attribute);
        }
        const nri::VertexInputDesc vertexInput{
            attributes.data(), static_cast<uint8_t>(attributes.size()),
            streams.data(), static_cast<uint8_t>(streams.size())};

        std::vector<uint32_t> vertex;
        std::vector<uint32_t> fragment;
        try {
            if (caster.VertexShader == nullptr ||
                !caster.VertexShader->VertexHooksAvailable()) {
                return reject();
            }
            const auto casterShader =
                BuildPicaDirectionalShadowCasterShader(
                    caster.VertexShader->Source,
                    caster.VertexShader->Key,
                    caster.VertexShader->VertexHooks);
            if (!casterShader.Applied()) {
                return reject();
            }
            vertex = Compile(Interop->Shaders(),
                casterShader.Source,
                Renderer::SpirvStage::Vertex,
                "oot3d_nri_directional_shadow.vert");
            fragment = Compile(Interop->Shaders(),
                kPicaDirectionalShadowFragmentShader,
                Renderer::SpirvStage::Fragment,
                "oot3d_nri_directional_shadow.frag");
        } catch (const std::exception&) {
            return reject();
        }
        const std::array<nri::ShaderDesc, 2> shaders{{
            {nri::StageBits::VERTEX_SHADER, vertex.data(),
             vertex.size() * sizeof(uint32_t)},
            {nri::StageBits::FRAGMENT_SHADER, fragment.data(),
             fragment.size() * sizeof(uint32_t)},
        }};
        nri::MultisampleDesc multisample{};
        multisample.sampleMask = 0xFFFFFFFFU;
        multisample.sampleNum = 1;
        nri::GraphicsPipelineDesc pipeline{};
        pipeline.pipelineLayout = ShadowLayout;
        pipeline.vertexInput = &vertexInput;
        pipeline.inputAssembly.topology = topology;
        pipeline.rasterization.fillMode = nri::FillMode::SOLID;
        pipeline.rasterization.cullMode = cull;
        pipeline.rasterization.frontCounterClockwise =
            caster.Material.FrontFace ==
            PicaSceneFrontFace::CounterClockwise;
        pipeline.rasterization.depthBias = {
            plan.DepthBiasConstant, 0.0F, plan.DepthBiasSlope};
        pipeline.multisample = &multisample;
        pipeline.outputMerger.depth.compareOp =
            nri::CompareOp::LESS_EQUAL;
        pipeline.outputMerger.depth.write = true;
        pipeline.outputMerger.depthStencilFormat =
            nri::Format::D32_SFLOAT;
        pipeline.shaders = shaders.data();
        pipeline.shaderNum = static_cast<uint32_t>(shaders.size());
        nri::Pipeline* result = nullptr;
        if (Core->CreateGraphicsPipeline(
                *NriInteropAccess::Device(*Interop), pipeline,
                result) != nri::Result::SUCCESS) {
            return reject();
        }
        ShadowPipelines.emplace(key, result);
        return result;
    }
#endif
};

NriDirectionalShadowPass::NriDirectionalShadowPass()
    : mImpl(std::make_unique<Impl>()) {}

NriDirectionalShadowPass::~NriDirectionalShadowPass() {
    Shutdown();
}

bool NriDirectionalShadowPass::Initialize(
    VkPhysicalDevice physicalDevice, VkDevice device,
    NriInteropContext& interop) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (const char* enabled =
            std::getenv("OOT3D_GRAPHICS_NRI_DIRECTIONAL_SHADOWS");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason =
            "NRI directional shadows disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    if (!interop.DynamicRenderingAvailable()) {
        mImpl->Reason =
            "NRI directional shadows require dynamic rendering";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI directional shadows were not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_D32_SFLOAT, &properties);
        const VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required) {
            throw std::runtime_error(
                "D32 sampled depth attachments are unavailable");
        }
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr) {
            throw std::runtime_error(
                "NRI directional shadow device is unavailable");
        }

        const std::array<nri::DescriptorRangeDesc, 2> ranges{{
            {0, 1, nri::DescriptorType::CONSTANT_BUFFER,
             nri::StageBits::VERTEX_SHADER},
            {1, 1, nri::DescriptorType::CONSTANT_BUFFER,
             nri::StageBits::VERTEX_SHADER},
        }};
        nri::DescriptorSetDesc descriptorSet{};
        descriptorSet.ranges = ranges.data();
        descriptorSet.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &descriptorSet;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::VERTEX_SHADER |
                              nri::StageBits::FRAGMENT_SHADER;
        layout.flags =
            nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->ShadowLayout) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI directional shadow layout creation failed");
        }

        VkSamplerCreateInfo samplerInfo{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0F;
        if (vkCreateSampler(
                device, &samplerInfo, nullptr,
                &mImpl->ReceiverSampler) != VK_SUCCESS) {
            throw std::runtime_error(
                "directional shadow receiver sampler creation failed");
        }

        const auto& deviceDesc =
            mImpl->Core->GetDeviceDesc(*nriDevice);
        const uint64_t uniformAlignment = std::max<uint64_t>(
            1U, deviceDesc.memoryAlignment.constantBufferOffset);
        for (auto& frame : mImpl->Frames) {
            frame.ShadowUniformStride = AlignUp(
                sizeof(ShadowTransformUniforms), uniformAlignment);
            nri::BufferDesc bufferDesc{};
            bufferDesc.size =
                frame.ShadowUniformStride * kMaximumCasters;
            bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
            if (mImpl->Core->CreateCommittedBuffer(
                    *nriDevice, nri::MemoryLocation::HOST_UPLOAD,
                    0.0F, bufferDesc,
                    frame.ShadowUniformBuffer) !=
                nri::Result::SUCCESS) {
                throw std::runtime_error(
                    "NRI directional shadow uniform arena creation failed");
            }
            frame.ShadowUniformMapped = static_cast<uint8_t*>(
                mImpl->Core->MapBuffer(
                    *frame.ShadowUniformBuffer, 0,
                    bufferDesc.size));
            if (frame.ShadowUniformMapped == nullptr) {
                throw std::runtime_error(
                    "NRI directional shadow uniform arena mapping failed");
            }
            frame.ShadowUniformViews.resize(kMaximumCasters, nullptr);
            for (uint32_t index = 0U; index < kMaximumCasters;
                 ++index) {
                nri::BufferViewDesc view{};
                view.buffer = frame.ShadowUniformBuffer;
                view.type = nri::BufferView::CONSTANT_BUFFER;
                view.offset = frame.ShadowUniformStride * index;
                view.size = sizeof(ShadowTransformUniforms);
                if (mImpl->Core->CreateBufferView(
                        view, frame.ShadowUniformViews[index]) !=
                    nri::Result::SUCCESS) {
                    throw std::runtime_error(
                        "NRI directional shadow uniform view creation failed");
                }
            }

            nri::DescriptorPoolDesc pool{};
            pool.descriptorSetMaxNum = kMaximumCasters;
            pool.constantBufferMaxNum = 2U * kMaximumCasters;
            frame.CasterSets.resize(kMaximumCasters);
            if (mImpl->Core->CreateDescriptorPool(
                    *nriDevice, pool, frame.CasterPool) !=
                    nri::Result::SUCCESS ||
                mImpl->Core->AllocateDescriptorSets(
                    *frame.CasterPool, *mImpl->ShadowLayout, 0,
                    frame.CasterSets.data(), kMaximumCasters, 0) !=
                    nri::Result::SUCCESS) {
                throw std::runtime_error(
                    "NRI directional shadow caster descriptors failed");
            }
        }
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
#endif
}

bool NriDirectionalShadowPass::ExecuteMap(
    const NriDirectionalShadowExecuteDesc& desc,
    const NativeSceneView& sceneView,
    const EffectPassBarrierPlan& shadowMapBarrierPlan) {
    mImpl->LastNativeLight = false;
    mImpl->LastCasters = 0U;
    mImpl->LastLight = {};
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::NotAttempted;
    mImpl->LastShadowMapBarrierExecution = {};
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    (void)sceneView;
    (void)shadowMapBarrierPlan;
    return false;
#else
    const auto* mapBarrier = shadowMapBarrierPlan.Find(
        EffectResource::DirectionalShadowMap);
    const auto expectedPassId = ::Fast::Renderer::StableExtensionPassId(
        PicaExtensionPassName(PicaExtensionPass::DirectionalShadowMap));
    if (!Available() ||
        !desc.ScheduleAuthorization.Matches(
            expectedPassId, EffectStage::AfterOpaque,
            desc.ExpectedSurface) ||
        desc.ExpectedSurface.Namespace != desc.RenderTargetNamespace ||
        desc.ExpectedSurface.Resource != desc.ColorPhysicalAddress ||
        desc.Settings.Mode != DirectionalShadowMode::SingleCascade ||
        !sceneView.Active() || sceneView.FrameId() != desc.FrameId ||
        sceneView.PicaFrame() == nullptr ||
        !sceneView.PicaFrame()->Active() ||
        sceneView.PicaFrame()->FrameId() != desc.FrameId ||
        sceneView.PicaFrame()->FrameSlot() != desc.FrameSlot ||
        !shadowMapBarrierPlan.Valid() || mapBarrier == nullptr ||
        mapBarrier->DispatchAccess != ResourceAccess::DepthAttachment ||
        mapBarrier->CompletionAccess != ResourceAccess::ShaderRead ||
        !mapBarrier->DownstreamRead) {
        return false;
    }
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::InputAccepted;

    const PicaSceneFrame& sceneFrame = *sceneView.PicaFrame();
    std::vector<const PicaSceneDrawRecord*> casters;
    casters.reserve(std::min<size_t>(
        sceneFrame.Draws().size(), kMaximumCasters));
    const auto sceneDraws = sceneFrame.Draws();
    for (size_t drawIndex = 0U; drawIndex < sceneDraws.size();
         ++drawIndex) {
        const auto& caster = sceneDraws[drawIndex];
        if (!sceneFrame.DirectionalShadowCaster(drawIndex) ||
            !caster.NativeTransform.CurrentViewToWorldAvailable ||
            !caster.NativeTransform.CurrentClipToWorldAvailable ||
            caster.RenderTarget.RenderTargetNamespace !=
                desc.RenderTargetNamespace ||
            caster.RenderTarget.ColorPhysicalAddress !=
                desc.ColorPhysicalAddress) {
            continue;
        }
        if (casters.size() == kMaximumCasters) {
            break;
        }
        casters.push_back(&caster);
    }
    if (casters.empty()) {
        return false;
    }
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::CastersMatched;
    const PicaDirectionalShadowLightSelection light =
        ResolvePicaDirectionalShadowLightSelection(casters);
    if (!light.Valid()) {
        return false;
    }
    mImpl->LastLight = {
        light.WorldDirectionTowardSource,
        light.CandidateCount,
        light.ClusterCount,
        light.GeometryLightWeight,
    };
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::NativeLightResolved;

    DirectionalShadowFrameInput frameInput;
    frameInput.ClipToWorld = light.ReferenceDraw->NativeTransform
                                 .CurrentClipToWorld;
    frameInput.LightDirectionTowardSource =
        light.WorldDirectionTowardSource;
    frameInput.Settings = desc.Settings;
    const auto plan = BuildDirectionalShadowFramePlan(frameInput);
    if (!plan.Ready()) {
        return false;
    }
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::FramePlanReady;

    const ShadowTargetKey targetKey{
        desc.RenderTargetNamespace, desc.ColorPhysicalAddress};
    Impl::TargetHistory* target = mImpl->EnsureTarget(
        targetKey, plan.Resolution, desc.FrameId);
    if (target == nullptr) {
        return false;
    }
    auto& historySlot =
        target->Slots[desc.FrameId % kFrameSlots];
    historySlot.State = {};
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::ResourcesReady;

    const uint32_t frameIndex = desc.FrameSlot % kFrameSlots;
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(
            *mImpl->Interop, desc.FrameSlot);
    auto& frame = mImpl->Frames[frameIndex];
    if (command == nullptr || frame.CasterPool == nullptr ||
        frame.ShadowUniformMapped == nullptr) {
        return false;
    }
    if (frame.FrameId != desc.FrameId) {
        for (nri::Descriptor* view : frame.TransientViews) {
            mImpl->Core->DestroyDescriptor(view);
        }
        frame.TransientViews.clear();
        frame.FrameId = desc.FrameId;
        frame.CastersUsed = 0U;
    }
    if (frame.CastersUsed + casters.size() >
        frame.CasterSets.size()) {
        return false;
    }

    const uintptr_t shadowKey =
        reinterpret_cast<uintptr_t>(historySlot.Texture.Image);
    mImpl->LastShadowMapBarrierExecution =
        shadowMapBarrierPlan.BeginOutputExecution(
            EffectResource::DirectionalShadowMap);
    const auto shadowWrite = mImpl->OwnedStates.PlanTransition(
        shadowKey, {mapBarrier->DispatchAccess, 0U});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameSlot,
            historySlot.Texture.Image, shadowWrite)) {
        return false;
    }
    mImpl->LastShadowMapBarrierExecution.RecordGraphTransition(
        shadowWrite);
    mImpl->OwnedStates.Commit(shadowWrite);

    nri::AttachmentDesc depthAttachment{};
    depthAttachment.descriptor =
        NriInteropAccess::DepthAttachmentView(
            *mImpl->Interop, historySlot.Texture.Image);
    if (depthAttachment.descriptor == nullptr) {
        return false;
    }
    depthAttachment.clearValue.depthStencil.depth = 1.0F;
    depthAttachment.loadOp = nri::LoadOp::CLEAR;
    depthAttachment.storeOp = nri::StoreOp::STORE;
    nri::RenderingDesc rendering{};
    rendering.depth = depthAttachment;
    mImpl->Core->CmdSetDescriptorPool(
        *command, *frame.CasterPool);
    mImpl->Core->CmdBeginRendering(*command, rendering);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::GRAPHICS,
        *mImpl->ShadowLayout);
    const nri::Viewport viewport{
        0.0F, 0.0F, static_cast<float>(plan.Resolution),
        static_cast<float>(plan.Resolution), 0.0F, 1.0F, true};
    const nri::Rect scissor{
        0, 0, static_cast<nri::Dim_t>(plan.Resolution),
        static_cast<nri::Dim_t>(plan.Resolution)};
    mImpl->Core->CmdSetViewports(*command, &viewport, 1);
    mImpl->Core->CmdSetScissors(*command, &scissor, 1);

    uint32_t renderedCasters = 0U;
    for (const PicaSceneDrawRecord* caster : casters) {
        const VkBuffer uniformHandle =
            FromSceneNativeHandle<VkBuffer>(
                caster->UniformBuffer.NativeHandle);
        const VkBuffer geometryHandle =
            FromSceneNativeHandle<VkBuffer>(
                caster->GeometryBuffer.NativeHandle);
        nri::Pipeline* pipeline =
            mImpl->GetOrCreatePipeline(*caster, plan);
        if (pipeline == nullptr ||
            !mImpl->Interop->WrapBuffer(
                uniformHandle, caster->UniformBuffer.Size) ||
            !mImpl->Interop->WrapBuffer(
                geometryHandle, caster->GeometryBuffer.Size)) {
            continue;
        }
        nri::Buffer* uniformBuffer = NriInteropAccess::Buffer(
            *mImpl->Interop, uniformHandle);
        nri::Buffer* vertexBuffer = NriInteropAccess::Buffer(
            *mImpl->Interop, geometryHandle);
        if (uniformBuffer == nullptr || vertexBuffer == nullptr ||
            caster->VertexUniformOffset >
                caster->UniformBuffer.Size ||
            caster->VertexUniformSize >
                caster->UniformBuffer.Size -
                    caster->VertexUniformOffset) {
            continue;
        }

        nri::Descriptor* vertexUniform = nullptr;
        nri::BufferViewDesc uniformView{};
        uniformView.buffer = uniformBuffer;
        uniformView.type = nri::BufferView::CONSTANT_BUFFER;
        uniformView.offset = caster->VertexUniformOffset;
        uniformView.size = caster->VertexUniformSize;
        if (mImpl->Core->CreateBufferView(
                uniformView, vertexUniform) !=
            nri::Result::SUCCESS) {
            continue;
        }
        frame.TransientViews.push_back(vertexUniform);

        const uint32_t casterSlot = frame.CastersUsed++;
        ShadowTransformUniforms transform;
        transform.ViewToWorld =
            caster->NativeTransform.CurrentViewToWorld;
        transform.WorldToLightClip = plan.WorldToLightClip;
        std::memcpy(
            frame.ShadowUniformMapped +
                frame.ShadowUniformStride * casterSlot,
            &transform, sizeof(transform));

        nri::DescriptorSet* set = frame.CasterSets[casterSlot];
        const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
            {set, 0, 0, &vertexUniform, 1},
            {set, 1, 0,
             &frame.ShadowUniformViews[casterSlot], 1},
        }};
        mImpl->Core->UpdateDescriptorRanges(
            updates.data(), static_cast<uint32_t>(updates.size()));
        mImpl->Core->CmdSetPipeline(*command, *pipeline);
        const nri::SetDescriptorSetDesc setDesc{
            0, set, nri::BindPoint::GRAPHICS};
        mImpl->Core->CmdSetDescriptorSet(*command, setDesc);

        bool validBindings = true;
        for (const auto& binding :
             sceneFrame.VertexBindings(*caster)) {
            if (binding.Size == 0U || binding.Stride == 0U ||
                binding.Offset > caster->GeometryBuffer.Size ||
                binding.Size > caster->GeometryBuffer.Size -
                                   binding.Offset) {
                validBindings = false;
                break;
            }
            const nri::VertexBufferDesc vertex{
                vertexBuffer, binding.Offset, binding.Stride};
            mImpl->Core->CmdSetVertexBuffers(
                *command, binding.Binding, &vertex, 1);
        }
        if (!validBindings) {
            continue;
        }
        if (caster->Indexed) {
            const uint64_t indexBytes =
                static_cast<uint64_t>(caster->VertexOrIndexCount) *
                sizeof(uint16_t);
            if (caster->IndexOffset >
                    caster->GeometryBuffer.Size ||
                indexBytes > caster->GeometryBuffer.Size -
                                 caster->IndexOffset) {
                continue;
            }
            mImpl->Core->CmdSetIndexBuffer(
                *command, *vertexBuffer, caster->IndexOffset,
                nri::IndexType::UINT16);
            const nri::DrawIndexedDesc draw{
                caster->VertexOrIndexCount, 1U, 0U,
                caster->BaseVertex, 0U};
            mImpl->Core->CmdDrawIndexed(*command, draw);
        } else {
            const nri::DrawDesc draw{
                caster->VertexOrIndexCount, 1U, 0U, 0U};
            mImpl->Core->CmdDraw(*command, draw);
        }
        ++renderedCasters;
    }
    mImpl->Core->CmdEndRendering(*command);
    if (renderedCasters == 0U) {
        return false;
    }
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::ShadowRasterized;

    const auto shadowRead = mImpl->OwnedStates.PlanTransition(
        shadowKey, {mapBarrier->CompletionAccess, 0U});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameSlot,
            historySlot.Texture.Image, shadowRead)) {
        return false;
    }
    mImpl->LastShadowMapBarrierExecution.RecordGraphTransition(
        shadowRead);
    mImpl->OwnedStates.Commit(shadowRead);

    historySlot.State.WorldToShadowTexture =
        plan.WorldToShadowTexture;
    historySlot.State.WorldLightDirectionTowardSource =
        plan.LightDirectionTowardSource;
    historySlot.State.ProducedFrameId = desc.FrameId;
    historySlot.State.Resolution = plan.Resolution;
    historySlot.State.Strength = plan.Strength;
    historySlot.State.DepthBias =
        std::max(plan.DepthBiasConstant, 0.0F) /
        static_cast<float>(plan.Resolution);
    historySlot.State.PcfRadius = plan.PcfRadius;
    target->LastUsedFrameId = desc.FrameId;

    mImpl->LastNativeLight = true;
    mImpl->LastCasters = renderedCasters;
    mImpl->LastStage =
        NriDirectionalShadowExecutionStage::HistoryPublished;
    return true;
#endif
}

NriDirectionalShadowHistorySnapshot
NriDirectionalShadowPass::FindLatestHistory(
    uint64_t renderTargetNamespace,
    uint32_t colorPhysicalAddress,
    uint64_t beforeFrameId) const noexcept {
    NriDirectionalShadowHistorySnapshot result;
    if (!Available() || beforeFrameId == 0U) {
        return result;
    }
    const auto found = mImpl->Histories.find({
        renderTargetNamespace, colorPhysicalAddress});
    if (found == mImpl->Histories.end()) {
        return result;
    }
    const Impl::HistorySlot* latest = nullptr;
    for (const auto& slot : found->second.Slots) {
        if (!slot.State.Valid() ||
            slot.State.ProducedFrameId >= beforeFrameId ||
            (latest != nullptr &&
             latest->State.ProducedFrameId >=
                 slot.State.ProducedFrameId)) {
            continue;
        }
        latest = &slot;
    }
    if (latest == nullptr) {
        return result;
    }
    result.Image = latest->Texture.Image;
    result.View = latest->Texture.SampledView;
    result.Sampler = mImpl->ReceiverSampler;
    result.State = latest->State;
    return result.Valid() ? result
                          : NriDirectionalShadowHistorySnapshot{};
}

void NriDirectionalShadowPass::InvalidateScreenResources() {
    if (mImpl->Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mImpl->Device);
    }
    mImpl->DestroyOwnedTextures();
}

void NriDirectionalShadowPass::Shutdown() {
    mImpl->DestroyOwnedTextures();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        for (auto& frame : mImpl->Frames) {
            for (nri::Descriptor* view : frame.TransientViews) {
                mImpl->Core->DestroyDescriptor(view);
            }
            if (frame.ShadowUniformMapped != nullptr &&
                frame.ShadowUniformBuffer != nullptr) {
                mImpl->Core->UnmapBuffer(
                    *frame.ShadowUniformBuffer);
            }
            for (nri::Descriptor* view :
                 frame.ShadowUniformViews) {
                if (view != nullptr) {
                    mImpl->Core->DestroyDescriptor(view);
                }
            }
            if (frame.ShadowUniformBuffer != nullptr) {
                mImpl->Core->DestroyBuffer(
                    frame.ShadowUniformBuffer);
            }
            if (frame.CasterPool != nullptr) {
                mImpl->Core->DestroyDescriptorPool(
                    frame.CasterPool);
            }
        }
        for (auto& [key, pipeline] : mImpl->ShadowPipelines) {
            (void)key;
            mImpl->Core->DestroyPipeline(pipeline);
        }
        if (mImpl->ShadowLayout != nullptr) {
            mImpl->Core->DestroyPipelineLayout(
                mImpl->ShadowLayout);
        }
    }
#endif
    if (mImpl->ReceiverSampler != VK_NULL_HANDLE &&
        mImpl->Device != VK_NULL_HANDLE) {
        vkDestroySampler(
            mImpl->Device, mImpl->ReceiverSampler, nullptr);
    }
    *mImpl = {};
    mImpl->Reason =
        "NRI directional shadow pass is not initialized";
}

bool NriDirectionalShadowPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr &&
           mImpl->Core != nullptr &&
           mImpl->ShadowLayout != nullptr &&
           mImpl->ReceiverSampler != VK_NULL_HANDLE;
#else
    return false;
#endif
}

bool NriDirectionalShadowPass::LastExecutionUsedNativeLight() const {
    return mImpl->LastNativeLight;
}

uint32_t NriDirectionalShadowPass::LastCasterCount() const {
    return mImpl->LastCasters;
}

const NriDirectionalShadowLightTelemetry&
NriDirectionalShadowPass::LastLightTelemetry() const {
    return mImpl->LastLight;
}

NriDirectionalShadowExecutionStage
NriDirectionalShadowPass::LastExecutionStage() const {
    return mImpl->LastStage;
}

const EffectPassBarrierExecution&
NriDirectionalShadowPass::LastShadowMapBarrierExecution() const {
    return mImpl->LastShadowMapBarrierExecution;
}

const std::string&
NriDirectionalShadowPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
