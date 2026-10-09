#include "fast/oot3d/nri_directional_shadow_pass.h"

#include "fast/oot3d/contact_shadows.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/pica_directional_shadow_caster.h"
#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/renderer3ds/virtual_shadow_keyframes.h"

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
#include <optional>
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
// Caster descriptor sets of one frame slot: each shadow layout (the two
// displayed and the one in construction) and the point maps bind a caster once.
// The contact shadows' camera depth binds the casters once more, for one
// target per frame (the arena's reserve).
constexpr uint32_t kCasterBindingsPerFrame =
    (Renderer3ds::kVirtualShadowKeyframeSlots + 2U) * kMaximumCasters;
// A still light releases the layouts it no longer displays after this long.
constexpr uint32_t kStillLayoutReleaseFrames = 600U;
constexpr size_t kMaximumShadowTargets = 8U;
constexpr uint32_t kMaximumContactTargets = 1U;

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
        value ^=
            static_cast<uint64_t>(key.ColorPhysicalAddress) + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        return static_cast<size_t>(value);
    }
};

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders, const std::string& source,
                              Renderer::SpirvStage kind, const char* name) {
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

template <typename T> uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(value));
}

using ::Fast::Renderer3ds::PicaCompareFunction;
using ::Fast::Renderer3ds::PicaSceneTextureBinding;
using ::Fast::Renderer3ds::PicaSceneTextureImageFormat;

// Descriptor ranges of the caster layout: the canonical program's own
// bindings (vertex uniforms 0, textures 1-3, fragment uniforms 4, split
// samplers 7-9, lighting LUT 13-14) plus the caster transform. Opaque casters
// write only the vertex uniforms and the transform.
enum CasterRange : uint32_t {
    VertexUniformRange,
    TransformRange,
    Texture0Range,
    Texture1Range,
    Texture2Range,
    FragmentUniformRange,
    Sampler0Range,
    Sampler1Range,
    Sampler2Range,
    LightingLutRange,
    LightingLutSamplerRange,
    CasterRangeCount,
};

// An alpha-tested caster runs its canonical fragment program up to the native
// alpha test; "Always" passes every fragment and stays a depth-only caster.
bool UsesAlphaCasterProgram(const PicaSceneDrawRecord& caster) noexcept {
    return caster.Material.AlphaTestEnabled && caster.Material.AlphaCompare != PicaCompareFunction::Always &&
           caster.FragmentShader != nullptr && caster.FragmentShader->FragmentHooksAvailable() &&
           caster.FragmentUniformSize != 0U;
}

VkFormat CasterTextureFormat(const PicaSceneTextureBinding& texture) noexcept {
    return texture.ImageFormat == PicaSceneTextureImageFormat::R32Uint ? VK_FORMAT_R32_UINT
                                                                        : VK_FORMAT_R8G8B8A8_UNORM;
}

#ifdef ENABLE_OOT3D_NRI
// The sampler the ordered draw binds for this texture: native filters (a mip
// filter only with more than one uploaded level), PICA wrap modes and the
// native LOD clamp within the uploaded chain. The LOD bias stays in the program.
nri::SamplerDesc CasterSamplerDesc(const PicaSceneTextureBinding& texture) noexcept {
    const auto address = [](uint8_t wrap) {
        switch (wrap) {
            case 2U:
            case 6U:
            case 7U:
                return nri::AddressMode::REPEAT;
            case 3U:
                return nri::AddressMode::MIRRORED_REPEAT;
            default:
                return nri::AddressMode::CLAMP_TO_EDGE;
        }
    };
    const uint32_t highestMip = texture.MipLevels > 0U ? texture.MipLevels - 1U : 0U;
    nri::SamplerDesc desc{};
    desc.filters = { texture.MinLinear ? nri::Filter::LINEAR : nri::Filter::NEAREST,
                     texture.MagLinear ? nri::Filter::LINEAR : nri::Filter::NEAREST,
                     texture.MipLinear && texture.MipLevels > 1U ? nri::Filter::LINEAR : nri::Filter::NEAREST };
    desc.mipMin = static_cast<float>(std::min<uint32_t>(texture.MinMipLevel, highestMip));
    desc.mipMax = static_cast<float>(std::min<uint32_t>(texture.MaxMipLevel, highestMip));
    desc.addressModes = { address(texture.NativeWrapS), address(texture.NativeWrapT), nri::AddressMode::REPEAT };
    desc.isInteger = texture.ImageFormat == PicaSceneTextureImageFormat::R32Uint;
    return desc;
}

uint64_t CasterSamplerKey(const nri::SamplerDesc& desc) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    hash = HashValue(hash, desc.filters.min);
    hash = HashValue(hash, desc.filters.mag);
    hash = HashValue(hash, desc.filters.mip);
    hash = HashValue(hash, desc.mipMin);
    hash = HashValue(hash, desc.mipMax);
    hash = HashValue(hash, desc.addressModes.u);
    hash = HashValue(hash, desc.addressModes.v);
    return HashValue(hash, desc.isInteger);
}
#endif

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

template <typename Handle> Handle FromSceneNativeHandle(uintptr_t value) noexcept {
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

    // One shadow layout (a keyframe of the light): its static depth, its
    // residency and the layout it was last prepared with.
    struct LayoutSlot {
        NriOwnedTexture2D StaticTexture;
        Renderer3ds::VirtualShadowMapCache Cache;
        Renderer3ds::VirtualShadowLayout Layout;
    };

    // Slot.Texture is the composed atlas: one region (a still light, slot 0)
    // or one per layout slot, side by side, each of the layout resolution.
    // Contact shadows of one target: the camera depth (reverse-Z) of its
    // casters and the visibility the receivers read.
    struct ContactTarget {
        NriOwnedTexture2D Depth;
        NriOwnedTexture2D Visibility;
        uint32_t Width = 0U;
        uint32_t Height = 0U;
    };

    struct TargetHistory {
        HistorySlot Slot;
        ContactTarget Contact;
        std::array<LayoutSlot, Renderer3ds::kVirtualShadowKeyframeSlots> Layouts;
        Renderer3ds::VirtualShadowKeyframes Keyframes;
        uint32_t Resolution = 0U;
        uint32_t Regions = 0U;
        uint32_t StillFrames = 0U;
        uint64_t LastUsedFrameId = 0U;
    };

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    uint32_t MaximumImageDimension = 0U;
    VkDevice Device = VK_NULL_HANDLE;
    VkSampler ReceiverSampler = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    std::unordered_map<ShadowTargetKey, TargetHistory, ShadowTargetKeyHash> Histories;
    struct RetiredTexture {
        NriOwnedTexture2D Texture;
        uint64_t FrameId;
    };
    std::vector<RetiredTexture> RetiredTextures;
    // Point-light cube atlases, one per target.
    struct PointTarget {
        NriOwnedTexture2D Texture;
        Renderer3ds::PointShadowLayout Layout;
        Renderer3ds::PointShadowSlots Slots{};
        std::array<uint64_t, Renderer3ds::kMaximumPointShadowLights> Signatures{};
        float Range = 0.0F;
        float Strength = 0.0F;
        uint64_t ProducedFrameId = 0U;
        uint64_t LastUsedFrameId = 0U;
    };
    std::unordered_map<ShadowTargetKey, PointTarget, ShadowTargetKeyHash> PointTargets;
    NriPointShadowTelemetry LastPoint;
    ResourceStateTracker OwnedStates;
    bool LastNativeLight = false;
    uint32_t LastCasters = 0U;
    uint32_t LastAlphaCasters = 0U;
    NriDirectionalShadowLightTelemetry LastLight;
    NriDirectionalShadowKeyframeTelemetry LastKeyframes;
    NriDirectionalShadowExecutionStage LastStage = NriDirectionalShadowExecutionStage::NotAttempted;
    EffectPassBarrierExecution LastShadowMapBarrierExecution;
    std::string Reason = "NRI directional shadow pass is not initialized";
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
        // Contact shadow compute sets, one per target and frame.
        nri::DescriptorPool* ContactPool = nullptr;
        std::vector<nri::DescriptorSet*> ContactSets;
        uint32_t ContactUsed = 0U;
    };
    std::array<FrameDescriptors, kFrameSlots> Frames;
    std::unordered_map<uint64_t, nri::Pipeline*> ShadowPipelines;
    std::unordered_set<uint64_t> FailedShadowPipelines;
    std::unordered_map<uint64_t, nri::Pipeline*> PointPipelines;
    std::unordered_set<uint64_t> FailedPointPipelines;
    std::unordered_map<uint64_t, nri::Descriptor*> CasterSamplers;
    // Contact shadows (Godot's screen space contact shadows compute).
    nri::PipelineLayout* ContactLayout = nullptr;
    std::array<nri::Pipeline*, kContactShadowLengthCount> ContactPipelines{};
    std::array<bool, kContactShadowLengthCount> FailedContactPipelines{};
    nri::Descriptor* ContactDepthSampler = nullptr;
#endif

    void RetireTexture(NriOwnedTexture2D& texture, uint64_t frameId) {
        if (texture.Image != VK_NULL_HANDLE) {
            OwnedStates.Forget(reinterpret_cast<uintptr_t>(texture.Image));
            RetiredTextures.push_back({ texture, frameId });
        }
        texture = {};
    }

    void Retire(TargetHistory& history, uint64_t frameId) {
        RetireTexture(history.Slot.Texture, frameId);
        RetireTexture(history.Contact.Depth, frameId);
        RetireTexture(history.Contact.Visibility, frameId);
        for (auto& layout : history.Layouts)
            RetireTexture(layout.StaticTexture, frameId);
        history = {};
    }

    void DestroyOwnedTextures() {
        if (Interop != nullptr) {
            for (auto& [key, history] : Histories) {
                (void)key;
                Interop->DestroyOwnedTexture(history.Slot.Texture.Image);
                Interop->DestroyOwnedTexture(history.Contact.Depth.Image);
                Interop->DestroyOwnedTexture(history.Contact.Visibility.Image);
                for (auto& layout : history.Layouts)
                    Interop->DestroyOwnedTexture(layout.StaticTexture.Image);
            }
            for (auto& texture : RetiredTextures) {
                Interop->DestroyOwnedTexture(texture.Texture.Image);
            }
            for (auto& [key, point] : PointTargets) {
                (void)key;
                Interop->DestroyOwnedTexture(point.Texture.Image);
            }
        }
        Histories.clear();
        PointTargets.clear();
        RetiredTextures.clear();
        OwnedStates.Clear();
    }

#ifdef ENABLE_OOT3D_NRI
    TargetHistory* EnsureTarget(const ShadowTargetKey& key, uint32_t resolution, uint64_t frameId) {
        // The host has waited for this frame slot's fence. An atlas last
        // referenced a full slot rotation ago can no longer be sampled.
        std::erase_if(RetiredTextures, [&](const auto& retired) {
            if (frameId < retired.FrameId || frameId - retired.FrameId < kFrameSlots)
                return false;
            Interop->DestroyOwnedTexture(retired.Texture.Image);
            return true;
        });
        auto found = Histories.find(key);
        if (found != Histories.end() && found->second.Resolution == resolution &&
            found->second.Slot.Texture.Image != VK_NULL_HANDLE) {
            found->second.LastUsedFrameId = frameId;
            return &found->second;
        }

        TargetHistory replacement;
        replacement.Resolution = resolution;
        replacement.Regions = 1U;
        replacement.LastUsedFrameId = frameId;
        // Keyframes need an atlas with a region per layout; a device without
        // it relayouts a moving light in steps.
        Renderer3ds::VirtualShadowKeyframeSettings keyframes;
        keyframes.Keyframed =
            uint64_t{ resolution } * Renderer3ds::kVirtualShadowKeyframeSlots <= MaximumImageDimension;
        replacement.Keyframes = Renderer3ds::VirtualShadowKeyframes(keyframes);
        if (!CreateAtlas(resolution, resolution, true, replacement.Slot.Texture))
            return nullptr;

        if (found != Histories.end()) {
            Retire(found->second, frameId);
            found->second = std::move(replacement);
            return &found->second;
        }
        if (Histories.size() >= kMaximumShadowTargets) {
            const auto oldest =
                std::min_element(Histories.begin(), Histories.end(), [](const auto& left, const auto& right) {
                    return left.second.LastUsedFrameId < right.second.LastUsedFrameId;
                });
            if (oldest != Histories.end()) {
                Retire(oldest->second, frameId);
                Histories.erase(oldest);
            }
        }
        return &Histories.emplace(key, std::move(replacement)).first->second;
    }

    // composed: sampled by receivers and restored from static depth;
    // otherwise the static depth of one layout.
    bool CreateAtlas(uint32_t width, uint32_t height, bool composed, NriOwnedTexture2D& texture) {
        NriOwnedTexture2DDesc textureDesc;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.Format = VK_FORMAT_D32_SFLOAT;
        textureDesc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            (composed ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        return Interop->CreateOwnedTexture2D(textureDesc, texture);
    }

    // Replaces the composed atlas by one of the given regions. The regions
    // of the retired atlas are lost: true when it was replaced.
    bool ResizeAtlas(TargetHistory& target, uint32_t regions, uint64_t frameId, bool& replaced) {
        replaced = false;
        if (regions == target.Regions)
            return true;
        NriOwnedTexture2D atlas;
        if (!CreateAtlas(target.Resolution * regions, target.Resolution, true, atlas))
            return false;
        RetireTexture(target.Slot.Texture, frameId);
        target.Slot.Texture = atlas;
        target.Regions = regions;
        replaced = true;
        return true;
    }

    uint64_t PipelineKey(const PicaSceneDrawRecord& caster, const DirectionalShadowFramePlan& plan,
                         bool camera = false) const {
        uint64_t hash = camera ? 0x6a09e667f3bcc909ULL : 1469598103934665603ULL;
        hash = HashValue(hash, caster.VertexShader->Key);
        hash = HashValue(hash, caster.Material.Topology);
        hash = HashValue(hash, plan.CullCasters && !camera ? caster.Material.CullMode : PicaSceneCullMode::None);
        hash = HashValue(hash, caster.Material.FrontFace);
        hash = HashValue(hash, plan.DepthBiasConstant);
        hash = HashValue(hash, plan.DepthBiasSlope);
        hash = HashValue(hash, caster.VertexLayout->StructuralKey);
        if (UsesAlphaCasterProgram(caster))
            hash = HashValue(hash, caster.FragmentShader->Key);
        return hash;
    }

    nri::Descriptor* SampledView(const PicaSceneTextureBinding& texture) {
        const auto image = FromSceneNativeHandle<VkImage>(texture.NativeImageHandle);
        if (image == VK_NULL_HANDLE || texture.ImageWidth == 0U || texture.ImageHeight == 0U ||
            texture.MipLevels == 0U ||
            !Interop->WrapTexture(image, CasterTextureFormat(texture), VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                  texture.ImageWidth, texture.ImageHeight, texture.MipLevels))
            return nullptr;
        return NriInteropAccess::TextureView(*Interop, image, false);
    }

    nri::Descriptor* Sampler(const PicaSceneTextureBinding& texture) {
        const auto desc = CasterSamplerDesc(texture);
        auto [found, inserted] = CasterSamplers.try_emplace(CasterSamplerKey(desc), nullptr);
        if (inserted &&
            Core->CreateSampler(*NriInteropAccess::Device(*Interop), desc, found->second) != nri::Result::SUCCESS) {
            CasterSamplers.erase(found);
            return nullptr;
        }
        return found->second;
    }

    // camera: the contact shadows' camera depth (reverse-Z, both faces, no
    // bias) instead of a shadow map page.
    nri::Pipeline* GetOrCreatePipeline(const PicaSceneDrawRecord& caster, const DirectionalShadowFramePlan& plan,
                                       bool camera = false) {
        const uint64_t key = PipelineKey(caster, plan, camera);
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
        const nri::Topology topology = ToNriTopology(caster.Material.Topology);
        const nri::CullMode cull = ToNriCullMode(plan.CullCasters && !camera ? caster.Material.CullMode
                                                                            : PicaSceneCullMode::None);
        if (topology == nri::Topology::MAX_NUM || cull == nri::CullMode::MAX_NUM) {
            return reject();
        }

        std::vector<nri::VertexStreamDesc> streams;
        streams.reserve(caster.VertexLayout->Bindings.size());
        for (const auto& source : caster.VertexLayout->Bindings) {
            if (source.Binding > std::numeric_limits<uint16_t>::max() ||
                source.ByteStride > std::numeric_limits<uint16_t>::max()) {
                return reject();
            }
            streams.push_back({
                static_cast<uint16_t>(source.Binding),
                source.PerInstance ? nri::VertexStreamStepRate::PER_INSTANCE : nri::VertexStreamStepRate::PER_VERTEX,
                static_cast<uint16_t>(source.ByteStride),
            });
        }
        std::vector<nri::VertexAttributeDesc> attributes;
        attributes.reserve(caster.VertexLayout->Attributes.size());
        for (const auto& source : caster.VertexLayout->Attributes) {
            if (source.Location > std::numeric_limits<uint16_t>::max() ||
                source.Binding > std::numeric_limits<uint16_t>::max()) {
                return reject();
            }
            nri::VertexAttributeDesc attribute{};
            attribute.vk.location = source.Location;
            attribute.offset = source.ByteOffset;
            attribute.format = nri::Format::RGBA32_SFLOAT;
            attribute.streamIndex = static_cast<uint16_t>(source.Binding);
            attributes.push_back(attribute);
        }
        const nri::VertexInputDesc vertexInput{ attributes.data(), static_cast<uint8_t>(attributes.size()),
                                                streams.data(), static_cast<uint8_t>(streams.size()) };

        std::vector<uint32_t> vertex;
        std::vector<uint32_t> fragment;
        try {
            if (caster.VertexShader == nullptr || !caster.VertexShader->VertexHooksAvailable()) {
                return reject();
            }
            const auto casterShader = BuildPicaDirectionalShadowCasterShader(
                caster.VertexShader->Source, caster.VertexShader->Key, caster.VertexShader->VertexHooks);
            if (!casterShader.Applied()) {
                return reject();
            }
            vertex = Compile(Interop->Shaders(), casterShader.Source, Renderer::SpirvStage::Vertex,
                             "oot3d_nri_directional_shadow.vert");
            if (UsesAlphaCasterProgram(caster)) {
                const auto alphaShader = BuildPicaShadowCasterAlphaFragmentShader(
                    caster.FragmentShader->Source, caster.FragmentShader->Key, caster.FragmentShader->FragmentHooks);
                if (!alphaShader.Applied()) {
                    return reject();
                }
                fragment = Compile(Interop->Shaders(), alphaShader.Source, Renderer::SpirvStage::Fragment,
                                   "oot3d_nri_shadow_caster_alpha.frag");
            } else {
                fragment = Compile(Interop->Shaders(), kPicaDirectionalShadowFragmentShader,
                                   Renderer::SpirvStage::Fragment, "oot3d_nri_directional_shadow.frag");
            }
        } catch (const std::exception&) { return reject(); }
        const std::array<nri::ShaderDesc, 2> shaders{ {
            { nri::StageBits::VERTEX_SHADER, vertex.data(), vertex.size() * sizeof(uint32_t) },
            { nri::StageBits::FRAGMENT_SHADER, fragment.data(), fragment.size() * sizeof(uint32_t) },
        } };
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
            caster.Material.FrontFace == PicaSceneFrontFace::CounterClockwise;
        if (!camera)
            pipeline.rasterization.depthBias = { plan.DepthBiasConstant, 0.0F, plan.DepthBiasSlope };
        pipeline.multisample = &multisample;
        pipeline.outputMerger.depth.compareOp = camera ? nri::CompareOp::GREATER : nri::CompareOp::LESS_EQUAL;
        pipeline.outputMerger.depth.write = true;
        pipeline.outputMerger.depthStencilFormat = nri::Format::D32_SFLOAT;
        pipeline.shaders = shaders.data();
        pipeline.shaderNum = static_cast<uint32_t>(shaders.size());
        nri::Pipeline* result = nullptr;
        pipeline.cache = NriInteropAccess::PipelineCache(*Interop);
        if (Core->CreateGraphicsPipeline(*NriInteropAccess::Device(*Interop), pipeline, result) !=
            nri::Result::SUCCESS) {
            return reject();
        }
        ShadowPipelines.emplace(key, result);
        return result;
    }

    PointTarget* EnsurePointTarget(const ShadowTargetKey& key, const Renderer3ds::PointShadowLayout& layout,
                                   uint64_t frameId) {
        auto found = PointTargets.find(key);
        if (found != PointTargets.end() && found->second.Layout.Width == layout.Width &&
            found->second.Layout.Height == layout.Height && found->second.Texture.Image != VK_NULL_HANDLE) {
            found->second.LastUsedFrameId = frameId;
            return &found->second;
        }
        PointTarget replacement;
        replacement.Layout = layout;
        replacement.LastUsedFrameId = frameId;
        NriOwnedTexture2DDesc textureDesc;
        textureDesc.Width = layout.Width;
        textureDesc.Height = layout.Height;
        textureDesc.Format = VK_FORMAT_D32_SFLOAT;
        textureDesc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!Interop->CreateOwnedTexture2D(textureDesc, replacement.Texture))
            return nullptr;
        if (found != PointTargets.end()) {
            OwnedStates.Forget(reinterpret_cast<uintptr_t>(found->second.Texture.Image));
            RetiredTextures.push_back({ found->second.Texture, frameId });
            found->second = std::move(replacement);
            return &found->second;
        }
        if (PointTargets.size() >= kMaximumShadowTargets) {
            const auto oldest = std::min_element(PointTargets.begin(), PointTargets.end(), [](const auto& l, const auto& r) {
                return l.second.LastUsedFrameId < r.second.LastUsedFrameId;
            });
            OwnedStates.Forget(reinterpret_cast<uintptr_t>(oldest->second.Texture.Image));
            RetiredTextures.push_back({ oldest->second.Texture, frameId });
            PointTargets.erase(oldest);
        }
        return &PointTargets.emplace(key, std::move(replacement)).first->second;
    }

    nri::Pipeline* GetOrCreatePointPipeline(const PicaSceneDrawRecord& caster) {
        uint64_t key = 0x504f494e54534844ULL;
        key = HashValue(key, caster.VertexShader->Key);
        key = HashValue(key, caster.Material.Topology);
        key = HashValue(key, caster.VertexLayout->StructuralKey);
        if (UsesAlphaCasterProgram(caster))
            key = HashValue(key, caster.FragmentShader->Key);
        if (const auto found = PointPipelines.find(key); found != PointPipelines.end())
            return found->second;
        if (FailedPointPipelines.contains(key))
            return nullptr;
        const auto reject = [this, key]() -> nri::Pipeline* {
            FailedPointPipelines.insert(key);
            return nullptr;
        };
        const nri::Topology topology = ToNriTopology(caster.Material.Topology);
        if (topology == nri::Topology::MAX_NUM || caster.VertexShader == nullptr ||
            !caster.VertexShader->VertexHooksAvailable())
            return reject();
        std::vector<nri::VertexStreamDesc> streams;
        for (const auto& source : caster.VertexLayout->Bindings) {
            if (source.Binding > std::numeric_limits<uint16_t>::max() ||
                source.ByteStride > std::numeric_limits<uint16_t>::max())
                return reject();
            streams.push_back({ static_cast<uint16_t>(source.Binding),
                                source.PerInstance ? nri::VertexStreamStepRate::PER_INSTANCE
                                                   : nri::VertexStreamStepRate::PER_VERTEX,
                                static_cast<uint16_t>(source.ByteStride) });
        }
        std::vector<nri::VertexAttributeDesc> attributes;
        for (const auto& source : caster.VertexLayout->Attributes) {
            if (source.Location > std::numeric_limits<uint16_t>::max() ||
                source.Binding > std::numeric_limits<uint16_t>::max())
                return reject();
            nri::VertexAttributeDesc attribute{};
            attribute.vk.location = source.Location;
            attribute.offset = source.ByteOffset;
            attribute.format = nri::Format::RGBA32_SFLOAT;
            attribute.streamIndex = static_cast<uint16_t>(source.Binding);
            attributes.push_back(attribute);
        }
        const nri::VertexInputDesc vertexInput{ attributes.data(), static_cast<uint8_t>(attributes.size()),
                                                streams.data(), static_cast<uint8_t>(streams.size()) };
        std::vector<uint32_t> vertex;
        std::vector<uint32_t> fragment;
        try {
            const auto casterShader = BuildPicaPointShadowCasterShader(
                caster.VertexShader->Source, caster.VertexShader->Key, caster.VertexShader->VertexHooks);
            if (!casterShader.Applied())
                return reject();
            vertex = Compile(Interop->Shaders(), casterShader.Source, Renderer::SpirvStage::Vertex,
                             "oot3d_nri_point_shadow.vert");
            if (UsesAlphaCasterProgram(caster)) {
                const auto alphaShader = BuildPicaShadowCasterAlphaFragmentShader(
                    caster.FragmentShader->Source, caster.FragmentShader->Key, caster.FragmentShader->FragmentHooks);
                if (!alphaShader.Applied())
                    return reject();
                fragment = Compile(Interop->Shaders(), alphaShader.Source, Renderer::SpirvStage::Fragment,
                                   "oot3d_nri_shadow_caster_alpha.frag");
            } else {
                fragment = Compile(Interop->Shaders(), kPicaDirectionalShadowFragmentShader,
                                   Renderer::SpirvStage::Fragment, "oot3d_nri_directional_shadow.frag");
            }
        } catch (const std::exception&) { return reject(); }
        const std::array<nri::ShaderDesc, 2> shaders{ {
            { nri::StageBits::VERTEX_SHADER, vertex.data(), vertex.size() * sizeof(uint32_t) },
            { nri::StageBits::FRAGMENT_SHADER, fragment.data(), fragment.size() * sizeof(uint32_t) },
        } };
        nri::MultisampleDesc multisample{};
        multisample.sampleMask = 0xFFFFFFFFU;
        multisample.sampleNum = 1;
        nri::GraphicsPipelineDesc pipeline{};
        pipeline.pipelineLayout = ShadowLayout;
        pipeline.vertexInput = &vertexInput;
        pipeline.inputAssembly.topology = topology;
        pipeline.rasterization.fillMode = nri::FillMode::SOLID;
        pipeline.rasterization.cullMode = nri::CullMode::NONE;
        pipeline.rasterization.depthBias = { 0.0F, 0.0F, 2.0F };
        pipeline.multisample = &multisample;
        pipeline.outputMerger.depth.compareOp = nri::CompareOp::LESS_EQUAL;
        pipeline.outputMerger.depth.write = true;
        pipeline.outputMerger.depthStencilFormat = nri::Format::D32_SFLOAT;
        pipeline.shaders = shaders.data();
        pipeline.shaderNum = static_cast<uint32_t>(shaders.size());
        nri::Pipeline* result = nullptr;
        pipeline.cache = NriInteropAccess::PipelineCache(*Interop);
        if (Core->CreateGraphicsPipeline(*NriInteropAccess::Device(*Interop), pipeline, result) != nri::Result::SUCCESS)
            return reject();
        PointPipelines.emplace(key, result);
        return result;
    }

    struct PreparedCaster {
        nri::Pipeline* Pipeline = nullptr;
        nri::Buffer* Geometry = nullptr;
        nri::DescriptorSet* Set = nullptr;
    };

    // Descriptors of one caster in this frame slot: vertex uniforms, the
    // caster transform and, for an alpha-tested caster, its native fragment
    // state. The set stays valid for every map recorded in the frame.
    bool BindCaster(FrameDescriptors& frame, const PicaSceneFrame& sceneFrame, const PicaSceneDrawRecord& caster,
                    const DirectionalShadowMatrix& worldToLightClip, PreparedCaster& draw) {
        if (frame.CastersUsed == frame.CasterSets.size())
            return false;
        const auto uniforms = FromSceneNativeHandle<VkBuffer>(caster.UniformBuffer.NativeHandle);
        const auto vertices = FromSceneNativeHandle<VkBuffer>(caster.GeometryBuffer.NativeHandle);
        if (!Interop->WrapBuffer(uniforms, caster.UniformBuffer.Size) ||
            !Interop->WrapBuffer(vertices, caster.GeometryBuffer.Size))
            return false;
        auto* uniformBuffer = NriInteropAccess::Buffer(*Interop, uniforms);
        draw.Geometry = NriInteropAccess::Buffer(*Interop, vertices);
        if (uniformBuffer == nullptr || draw.Geometry == nullptr ||
            caster.VertexUniformOffset > caster.UniformBuffer.Size ||
            caster.VertexUniformSize > caster.UniformBuffer.Size - caster.VertexUniformOffset)
            return false;
        for (const auto& binding : sceneFrame.VertexBindings(caster))
            if (binding.Size == 0 || binding.Stride == 0 || binding.Offset > caster.GeometryBuffer.Size ||
                binding.Size > caster.GeometryBuffer.Size - binding.Offset)
                return false;
        if (caster.Indexed &&
            (caster.IndexOffset > caster.GeometryBuffer.Size ||
             uint64_t(caster.VertexOrIndexCount) * sizeof(uint16_t) > caster.GeometryBuffer.Size - caster.IndexOffset))
            return false;
        ShadowTransformUniforms transform;
        if (!InvertDirectionalShadowMatrix(*caster.LightingWorldToView, transform.ViewToWorld))
            return false;
        transform.WorldToLightClip = worldToLightClip;
        nri::BufferViewDesc uniformView{};
        uniformView.buffer = uniformBuffer;
        uniformView.type = nri::BufferView::CONSTANT_BUFFER;
        uniformView.offset = caster.VertexUniformOffset;
        uniformView.size = caster.VertexUniformSize;
        nri::Descriptor* vertexUniform = nullptr;
        if (Core->CreateBufferView(uniformView, vertexUniform) != nri::Result::SUCCESS)
            return false;
        frame.TransientViews.push_back(vertexUniform);
        const uint32_t slot = frame.CastersUsed++;
        std::memcpy(frame.ShadowUniformMapped + frame.ShadowUniformStride * slot, &transform, sizeof(transform));
        draw.Set = frame.CasterSets[slot];
        std::array<nri::UpdateDescriptorRangeDesc, CasterRangeCount> updates{};
        uint32_t updateCount = 0;
        updates[updateCount++] = { draw.Set, VertexUniformRange, 0, &vertexUniform, 1 };
        updates[updateCount++] = { draw.Set, TransformRange, 0, &frame.ShadowUniformViews[slot], 1 };
        // The alpha program reads its native textures, samplers, fragment
        // uniforms and, when declared, the lighting LUT, as the ordered draw does.
        std::array<nri::Descriptor*, 3> textures{};
        std::array<nri::Descriptor*, 3> samplers{};
        nri::Descriptor* fragmentUniform = nullptr;
        nri::Descriptor* lightingLut = nullptr;
        nri::Descriptor* lightingLutSampler = nullptr;
        if (UsesAlphaCasterProgram(caster)) {
            if (caster.FragmentUniformOffset > caster.UniformBuffer.Size ||
                caster.FragmentUniformSize > caster.UniformBuffer.Size - caster.FragmentUniformOffset)
                return false;
            nri::BufferViewDesc fragmentView{};
            fragmentView.buffer = uniformBuffer;
            fragmentView.type = nri::BufferView::CONSTANT_BUFFER;
            fragmentView.offset = caster.FragmentUniformOffset;
            fragmentView.size = caster.FragmentUniformSize;
            if (Core->CreateBufferView(fragmentView, fragmentUniform) != nri::Result::SUCCESS)
                return false;
            frame.TransientViews.push_back(fragmentUniform);
            for (size_t unit = 0; unit < textures.size(); ++unit) {
                textures[unit] = SampledView(caster.Textures[unit]);
                samplers[unit] = Sampler(caster.Textures[unit]);
                if (textures[unit] == nullptr || samplers[unit] == nullptr)
                    return false;
                updates[updateCount++] = { draw.Set, static_cast<uint32_t>(Texture0Range + unit), 0, &textures[unit], 1 };
                updates[updateCount++] = { draw.Set, static_cast<uint32_t>(Sampler0Range + unit), 0, &samplers[unit], 1 };
            }
            updates[updateCount++] = { draw.Set, FragmentUniformRange, 0, &fragmentUniform, 1 };
            if (caster.LightingLut.Bound) {
                lightingLut = SampledView(caster.LightingLut);
                lightingLutSampler = Sampler(caster.LightingLut);
                if (lightingLut == nullptr || lightingLutSampler == nullptr)
                    return false;
                updates[updateCount++] = { draw.Set, LightingLutRange, 0, &lightingLut, 1 };
                updates[updateCount++] = { draw.Set, LightingLutSamplerRange, 0, &lightingLutSampler, 1 };
            }
        }
        Core->UpdateDescriptorRanges(updates.data(), updateCount);
        return true;
    }

    // Godot's contact shadow compute for a length (2 + length reads).
    nri::Pipeline* ContactPipeline(uint32_t length) {
        if (length >= ContactPipelines.size() || FailedContactPipelines[length])
            return nullptr;
        if (ContactPipelines[length] != nullptr)
            return ContactPipelines[length];
        try {
            const std::string name = "oot3d_contact_shadows_" + std::to_string(length) + ".comp";
            const auto shader = Compile(Interop->Shaders(), ContactShadowComputeShader(length),
                                        Renderer::SpirvStage::Compute, name.c_str());
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = ContactLayout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = shader.data();
            pipeline.shader.size = shader.size() * sizeof(uint32_t);
            pipeline.cache = NriInteropAccess::PipelineCache(*Interop);
            if (Core->CreateComputePipeline(*NriInteropAccess::Device(*Interop), pipeline,
                                            ContactPipelines[length]) != nri::Result::SUCCESS) {
                ContactPipelines[length] = nullptr;
                FailedContactPipelines[length] = true;
            }
        } catch (const std::exception&) {
            FailedContactPipelines[length] = true;
        }
        return ContactPipelines[length];
    }

    // The contact shadows of a target: its casters' camera depth (reverse-Z,
    // both faces), then Godot's screen space contact shadows toward the
    // light into the visibility the receivers read (graph resource
    // ContactShadowMap). The depth is private to the pass.
    bool RenderContactShadows(const NriDirectionalShadowExecuteDesc& desc, nri::CommandBuffer& command,
                              FrameDescriptors& frame, const PicaSceneFrame& sceneFrame,
                              std::span<const PicaSceneDrawRecord*> drawable, const DirectionalShadowFramePlan& plan,
                              TargetHistory& target, const DirectionalShadowMatrix& worldToContactClip,
                              const Renderer3ds::ShadowVector& lightDirection, const EffectPassOutputBarrier& barrier) {
        if (ContactLayout == nullptr || ContactDepthSampler == nullptr || frame.ContactPool == nullptr ||
            frame.ContactUsed >= frame.ContactSets.size())
            return false;
        auto& contact = target.Contact;
        const uint32_t scale = plan.ContactShadows.HalfResolution ? 2U : 1U;
        const uint32_t width = (desc.ScreenWidth + scale - 1U) / scale;
        const uint32_t height = (desc.ScreenHeight + scale - 1U) / scale;
        if (contact.Width != width || contact.Height != height || contact.Depth.Image == VK_NULL_HANDLE ||
            contact.Visibility.Image == VK_NULL_HANDLE) {
            RetireTexture(contact.Depth, desc.FrameId);
            RetireTexture(contact.Visibility, desc.FrameId);
            contact = {};
            NriOwnedTexture2DDesc depthDesc;
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.Format = VK_FORMAT_D32_SFLOAT;
            depthDesc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            NriOwnedTexture2DDesc visibilityDesc;
            visibilityDesc.Width = width;
            visibilityDesc.Height = height;
            visibilityDesc.Format = VK_FORMAT_R8_UNORM;
            visibilityDesc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            if (!Interop->CreateOwnedTexture2D(depthDesc, contact.Depth) ||
                !Interop->CreateOwnedTexture2D(visibilityDesc, contact.Visibility)) {
                RetireTexture(contact.Depth, desc.FrameId);
                RetireTexture(contact.Visibility, desc.FrameId);
                contact = {};
                return false;
            }
            contact.Width = width;
            contact.Height = height;
        }
        const uint32_t length = std::min<uint32_t>(plan.ContactShadows.Length, kContactShadowLengthCount - 1U);
        nri::Pipeline* compute = ContactPipeline(length);
        const auto dispatch =
            ResolveContactShadowDispatch(worldToContactClip, lightDirection, width, height, plan.ContactShadows,
                                         desc.TemporalFrame);
        if (compute == nullptr || !dispatch.Valid())
            return false;

        // Every caster once more, with the contact camera.
        std::array<PreparedCaster, kMaximumCasters> draws{};
        for (size_t index = 0; index < drawable.size(); ++index) {
            auto* pipeline = GetOrCreatePipeline(*drawable[index], plan, true);
            if (pipeline == nullptr)
                continue;
            if (!BindCaster(frame, sceneFrame, *drawable[index], worldToContactClip, draws[index]))
                return false;
            draws[index].Pipeline = pipeline;
        }
        const auto transition = [&](VkImage image, ResourceAccess access, bool graph) {
            const auto change = OwnedStates.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
            if (!NriInteropAccess::CmdTextureBarrier(*Interop, desc.FrameSlot, image, change))
                return false;
            if (graph)
                LastShadowMapBarrierExecution.RecordGraphTransition(change);
            else
                LastShadowMapBarrierExecution.RecordPrivateTransition(change);
            OwnedStates.Commit(change);
            return true;
        };
        if (desc.ProfileMark)
            desc.ProfileMark("contact_depth");
        if (!transition(contact.Depth.Image, ResourceAccess::DepthAttachment, false))
            return false;
        nri::RenderingDesc rendering{};
        rendering.depth.descriptor = NriInteropAccess::DepthAttachmentView(*Interop, contact.Depth.Image);
        if (rendering.depth.descriptor == nullptr)
            return false;
        // Reverse-Z: cleared to the far plane.
        rendering.depth.loadOp = nri::LoadOp::CLEAR;
        rendering.depth.clearValue.depthStencil.depth = 0.0F;
        rendering.depth.storeOp = nri::StoreOp::STORE;
        Core->CmdSetDescriptorPool(command, *frame.CasterPool);
        Core->CmdBeginRendering(command, rendering);
        Core->CmdSetPipelineLayout(command, nri::BindPoint::GRAPHICS, *ShadowLayout);
        const nri::Viewport viewport{ 0.0F, 0.0F, float(width), float(height), 0, 1, true };
        const nri::Rect scissor{ 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        Core->CmdSetViewports(command, &viewport, 1);
        Core->CmdSetScissors(command, &scissor, 1);
        const std::array<float, 4> identity{ 1.0F, 1.0F, 0.0F, 0.0F };
        const nri::SetRootConstantsDesc constants{ 0, identity.data(), sizeof(identity) };
        Core->CmdSetRootConstants(command, constants);
        nri::Pipeline* boundPipeline = nullptr;
        for (size_t index = 0; index < drawable.size(); ++index) {
            const auto& draw = draws[index];
            if (draw.Pipeline == nullptr)
                continue;
            const auto& caster = *drawable[index];
            if (boundPipeline != draw.Pipeline) {
                Core->CmdSetPipeline(command, *draw.Pipeline);
                boundPipeline = draw.Pipeline;
            }
            const nri::SetDescriptorSetDesc set{ 0, draw.Set, nri::BindPoint::GRAPHICS };
            Core->CmdSetDescriptorSet(command, set);
            for (const auto& binding : sceneFrame.VertexBindings(caster)) {
                const nri::VertexBufferDesc vertex{ draw.Geometry, binding.Offset, binding.Stride };
                Core->CmdSetVertexBuffers(command, binding.Binding, &vertex, 1);
            }
            if (caster.Indexed) {
                Core->CmdSetIndexBuffer(command, *draw.Geometry, caster.IndexOffset, nri::IndexType::UINT16);
                const nri::DrawIndexedDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, caster.BaseVertex, 0 };
                Core->CmdDrawIndexed(command, nativeDraw);
            } else {
                const nri::DrawDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, 0 };
                Core->CmdDraw(command, nativeDraw);
            }
        }
        Core->CmdEndRendering(command);
        if (desc.ProfileMark)
            desc.ProfileMark("contact_compute");

        if (!transition(contact.Depth.Image, ResourceAccess::ShaderRead, false) ||
            !transition(contact.Visibility.Image, barrier.DispatchAccess, true))
            return false;
        nri::Descriptor* depthView = NriInteropAccess::TextureView(*Interop, contact.Depth.Image, false);
        nri::Descriptor* storage = NriInteropAccess::TextureView(*Interop, contact.Visibility.Image, true);
        if (depthView == nullptr || storage == nullptr)
            return false;
        nri::DescriptorSet* set = frame.ContactSets[frame.ContactUsed++];
        const std::array<nri::UpdateDescriptorRangeDesc, 3> updates{ {
            { set, 0, 0, &depthView, 1 },
            { set, 1, 0, &storage, 1 },
            { set, 2, 0, &ContactDepthSampler, 1 },
        } };
        Core->UpdateDescriptorRanges(updates.data(), static_cast<uint32_t>(updates.size()));
        Core->CmdSetDescriptorPool(command, *frame.ContactPool);
        Core->CmdSetPipelineLayout(command, nri::BindPoint::COMPUTE, *ContactLayout);
        Core->CmdSetPipeline(command, *compute);
        const nri::SetDescriptorSetDesc computeSet{ 0, set, nri::BindPoint::COMPUTE };
        Core->CmdSetDescriptorSet(command, computeSet);
        const nri::SetRootConstantsDesc push{ 0, &dispatch.Constants, sizeof(dispatch.Constants), 0,
                                              nri::BindPoint::COMPUTE };
        Core->CmdSetRootConstants(command, push);
        const nri::DispatchDesc groups{ dispatch.Groups[0], dispatch.Groups[1], dispatch.Groups[2] };
        Core->CmdDispatch(command, groups);
        return transition(contact.Visibility.Image, barrier.CompletionAccess, true);
    }
#endif
};

NriDirectionalShadowPass::NriDirectionalShadowPass() : mImpl(std::make_unique<Impl>()) {
}

NriDirectionalShadowPass::~NriDirectionalShadowPass() {
    Shutdown();
}

bool NriDirectionalShadowPass::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                          NriInteropContext& interop) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (const char* enabled = std::getenv("OOT3D_GRAPHICS_NRI_DIRECTIONAL_SHADOWS");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason = "NRI directional shadows disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    if (!interop.DynamicRenderingAvailable()) {
        mImpl->Reason = "NRI directional shadows require dynamic rendering";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI directional shadows were not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT, &properties);
        const VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required) {
            throw std::runtime_error("D32 sampled depth attachments are unavailable");
        }
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        mImpl->MaximumImageDimension = deviceProperties.limits.maxImageDimension2D;
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr) {
            throw std::runtime_error("NRI directional shadow device is unavailable");
        }

        const std::array<nri::DescriptorRangeDesc, CasterRangeCount> ranges{ {
            { 0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::VERTEX_SHADER },
            { kPicaShadowCasterTransformBinding, 1, nri::DescriptorType::CONSTANT_BUFFER,
              nri::StageBits::VERTEX_SHADER },
            { 1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER },
            { 2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER },
            { 3, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER },
            { 4, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::FRAGMENT_SHADER },
            { 7, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER },
            { 8, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER },
            { 9, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER },
            { 13, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER },
            { 14, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER },
        } };
        nri::DescriptorSetDesc descriptorSet{};
        descriptorSet.ranges = ranges.data();
        descriptorSet.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::PipelineLayoutDesc layout{};
        const nri::RootConstantDesc pageConstants{ 0, sizeof(float) * 16, nri::StageBits::VERTEX_SHADER };
        layout.rootConstants = &pageConstants;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &descriptorSet;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(*nriDevice, layout, mImpl->ShadowLayout) != nri::Result::SUCCESS) {
            throw std::runtime_error("NRI directional shadow layout creation failed");
        }

        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0F;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &mImpl->ReceiverSampler) != VK_SUCCESS) {
            throw std::runtime_error("directional shadow receiver sampler creation failed");
        }

        {
            // Contact shadows: camera depth (texture + sampler) in, R8
            // visibility (storage) out, Godot's push constants.
            const std::array<nri::DescriptorRangeDesc, 3> contactRanges{ {
                { 0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
                { 1, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
                { 2, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER },
            } };
            nri::DescriptorSetDesc contactSet{};
            contactSet.ranges = contactRanges.data();
            contactSet.rangeNum = static_cast<uint32_t>(contactRanges.size());
            nri::RootConstantDesc contactConstants{};
            contactConstants.size = sizeof(ContactShadowPushConstants);
            contactConstants.shaderStages = nri::StageBits::COMPUTE_SHADER;
            nri::PipelineLayoutDesc contactLayout{};
            contactLayout.rootConstants = &contactConstants;
            contactLayout.rootConstantNum = 1;
            contactLayout.descriptorSets = &contactSet;
            contactLayout.descriptorSetNum = 1;
            contactLayout.shaderStages = nri::StageBits::COMPUTE_SHADER;
            contactLayout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
            // Optional: without them the shadow map works alone. The
            // visibility is a filtered R8 storage image.
            VkFormatProperties r8{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8_UNORM, &r8);
            const VkFormatFeatureFlags r8Needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            if ((r8.optimalTilingFeatures & r8Needed) != r8Needed ||
                mImpl->Core->CreatePipelineLayout(*nriDevice, contactLayout, mImpl->ContactLayout) !=
                    nri::Result::SUCCESS)
                mImpl->ContactLayout = nullptr;
            // Outside the screen reads the far plane (reverse-Z 0), as
            // Godot's border sampler.
            nri::SamplerDesc border{};
            border.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
            border.addressModes = { nri::AddressMode::CLAMP_TO_BORDER, nri::AddressMode::CLAMP_TO_BORDER,
                                    nri::AddressMode::CLAMP_TO_BORDER };
            border.borderColor.f = { 0.0F, 0.0F, 0.0F, 0.0F };
            if (mImpl->ContactLayout != nullptr &&
                mImpl->Core->CreateSampler(*nriDevice, border, mImpl->ContactDepthSampler) != nri::Result::SUCCESS)
                mImpl->ContactDepthSampler = nullptr;
        }

        const auto& deviceDesc = mImpl->Core->GetDeviceDesc(*nriDevice);
        const uint64_t uniformAlignment = std::max<uint64_t>(1U, deviceDesc.memoryAlignment.constantBufferOffset);
        for (auto& frame : mImpl->Frames) {
            frame.ShadowUniformStride = AlignUp(sizeof(ShadowTransformUniforms), uniformAlignment);
            nri::BufferDesc bufferDesc{};
            bufferDesc.size = frame.ShadowUniformStride * kCasterBindingsPerFrame;
            bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
            if (mImpl->Core->CreateCommittedBuffer(*nriDevice, nri::MemoryLocation::HOST_UPLOAD, 0.0F, bufferDesc,
                                                   frame.ShadowUniformBuffer) != nri::Result::SUCCESS) {
                throw std::runtime_error("NRI directional shadow uniform arena creation failed");
            }
            frame.ShadowUniformMapped =
                static_cast<uint8_t*>(mImpl->Core->MapBuffer(*frame.ShadowUniformBuffer, 0, bufferDesc.size));
            if (frame.ShadowUniformMapped == nullptr) {
                throw std::runtime_error("NRI directional shadow uniform arena mapping failed");
            }
            frame.ShadowUniformViews.resize(kCasterBindingsPerFrame, nullptr);
            for (uint32_t index = 0U; index < kCasterBindingsPerFrame; ++index) {
                nri::BufferViewDesc view{};
                view.buffer = frame.ShadowUniformBuffer;
                view.type = nri::BufferView::CONSTANT_BUFFER;
                view.offset = frame.ShadowUniformStride * index;
                view.size = sizeof(ShadowTransformUniforms);
                if (mImpl->Core->CreateBufferView(view, frame.ShadowUniformViews[index]) != nri::Result::SUCCESS) {
                    throw std::runtime_error("NRI directional shadow uniform view creation failed");
                }
            }

            nri::DescriptorPoolDesc pool{};
            pool.descriptorSetMaxNum = kCasterBindingsPerFrame;
            pool.constantBufferMaxNum = 3U * kCasterBindingsPerFrame;
            pool.textureMaxNum = 4U * kCasterBindingsPerFrame;
            pool.samplerMaxNum = 4U * kCasterBindingsPerFrame;
            frame.CasterSets.resize(kCasterBindingsPerFrame);
            if (mImpl->Core->CreateDescriptorPool(*nriDevice, pool, frame.CasterPool) != nri::Result::SUCCESS ||
                mImpl->Core->AllocateDescriptorSets(*frame.CasterPool, *mImpl->ShadowLayout, 0, frame.CasterSets.data(),
                                                    kCasterBindingsPerFrame, 0) != nri::Result::SUCCESS) {
                throw std::runtime_error("NRI directional shadow caster descriptors failed");
            }
            nri::DescriptorPoolDesc contactPool{};
            contactPool.descriptorSetMaxNum = kMaximumContactTargets;
            contactPool.textureMaxNum = kMaximumContactTargets;
            contactPool.storageTextureMaxNum = kMaximumContactTargets;
            contactPool.samplerMaxNum = kMaximumContactTargets;
            frame.ContactSets.resize(kMaximumContactTargets);
            if (mImpl->ContactLayout == nullptr || mImpl->ContactDepthSampler == nullptr ||
                mImpl->Core->CreateDescriptorPool(*nriDevice, contactPool, frame.ContactPool) != nri::Result::SUCCESS ||
                mImpl->Core->AllocateDescriptorSets(*frame.ContactPool, *mImpl->ContactLayout, 0,
                                                    frame.ContactSets.data(), kMaximumContactTargets,
                                                    0) != nri::Result::SUCCESS) {
                // This frame slot has no contact shadows; the map is unaffected.
                if (frame.ContactPool != nullptr)
                    mImpl->Core->DestroyDescriptorPool(frame.ContactPool);
                frame.ContactPool = nullptr;
                frame.ContactSets.clear();
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

bool NriDirectionalShadowPass::ExecuteMap(const NriDirectionalShadowExecuteDesc& desc, const NativeSceneView& sceneView,
                                          const EffectPassBarrierPlan& shadowMapBarrierPlan) {
    mImpl->LastNativeLight = false;
    mImpl->LastCasters = 0;
    mImpl->LastAlphaCasters = 0;
    mImpl->LastLight = {};
    mImpl->LastKeyframes = {};
    mImpl->LastStage = NriDirectionalShadowExecutionStage::NotAttempted;
    mImpl->LastShadowMapBarrierExecution = {};
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    (void)sceneView;
    (void)shadowMapBarrierPlan;
    return false;
#else
    const auto* mapBarrier = shadowMapBarrierPlan.Find(EffectResource::DirectionalShadowMap);
    const auto* staticBarrier = shadowMapBarrierPlan.Find(EffectResource::DirectionalShadowStaticCache);
    // Declared only while contact shadows are requested.
    const auto* contactBarrier = shadowMapBarrierPlan.Find(EffectResource::ContactShadowMap);
    const auto expectedPassId =
        ::Fast::Renderer::StableExtensionPassId(PicaExtensionPassName(PicaExtensionPass::DirectionalShadowMap));
    if (!Available() ||
        (!desc.ScheduleAuthorization.Matches(expectedPassId, EffectStage::BeforeOpaque, desc.ExpectedSurface) &&
         !desc.ScheduleAuthorization.Matches(expectedPassId, EffectStage::BeforeUi, desc.ExpectedSurface)) ||
        desc.ExpectedSurface.Namespace != desc.RenderTargetNamespace ||
        desc.ExpectedSurface.Resource != desc.ColorPhysicalAddress ||
        desc.Settings.Mode != DirectionalShadowMode::VirtualShadowMaps || !sceneView.Active() ||
        sceneView.FrameId() != desc.FrameId || sceneView.PicaFrame() == nullptr || !sceneView.PicaFrame()->Active() ||
        sceneView.PicaFrame()->FrameId() != desc.FrameId || sceneView.PicaFrame()->FrameSlot() != desc.FrameSlot ||
        !shadowMapBarrierPlan.Valid() || mapBarrier == nullptr || staticBarrier == nullptr ||
        staticBarrier->DispatchAccess != ResourceAccess::DepthAttachment ||
        staticBarrier->CompletionAccess != ResourceAccess::TransferRead ||
        mapBarrier->DispatchAccess != ResourceAccess::TransferWrite ||
        mapBarrier->IntermediateAccess != ResourceAccess::DepthAttachment ||
        mapBarrier->CompletionAccess != ResourceAccess::ShaderRead || !mapBarrier->DownstreamRead)
        return false;
    mImpl->LastStage = NriDirectionalShadowExecutionStage::InputAccepted;

    const auto& sceneFrame = *sceneView.PicaFrame();
    std::array<const PicaSceneDrawRecord*, kMaximumCasters> casterStorage{};
    size_t casterCount = 0;
    const auto sceneDraws = sceneFrame.Draws();
    for (size_t index = 0; index < sceneDraws.size(); ++index) {
        const auto& caster = sceneDraws[index];
        if (!sceneFrame.DirectionalShadowCaster(index) || !caster.NativeTransform.CurrentViewToWorldAvailable ||
            !caster.NativeTransform.CurrentClipToWorldAvailable || !caster.LightingWorldToView ||
            caster.RenderTarget.RenderTargetNamespace != desc.RenderTargetNamespace ||
            caster.RenderTarget.ColorPhysicalAddress != desc.ColorPhysicalAddress)
            continue;
        if (casterCount == casterStorage.size())
            return false;
        casterStorage[casterCount++] = &caster;
    }
    const auto casters = std::span(casterStorage).first(casterCount);
    if (casters.empty())
        return false;
    mImpl->LastStage = NriDirectionalShadowExecutionStage::CastersMatched;
    const auto sceneCaster = std::find_if(casters.begin(), casters.end(),
                                          [](const auto* draw) { return draw->SceneDirectionals != nullptr; });
    const auto* scene = sceneCaster != casters.end() ? (*sceneCaster)->SceneDirectionals.get() : nullptr;
    PicaDirectionalShadowLightSelection light;
    const bool authoredKey = desc.EnvironmentKey && desc.EnvironmentKey->Intensity > 0.0F;
    if (authoredKey) {
        light.WorldDirectionTowardSource = desc.EnvironmentKey->DirectionTowardSource;
        // The same direction the surface lighting resolves for these draws.
        if (desc.Environment != nullptr) {
            light.WorldDirectionTowardSource = Renderer3ds::ResolvePicaEnvironmentKeyDirection(*desc.Environment, scene);
            // A key that follows the scene's lights shows its main light's
            // shadow strength (horizon, handover).
            if (scene != nullptr && desc.Environment->KeyDirection != Renderer3ds::PicaEnvironmentKeyDirection::Authored)
                if (const auto* main = Renderer3ds::FindPicaSceneMainDirectional(*scene))
                    light.ShadowStrength = Renderer3ds::PicaSceneMainShadowStrength(*scene, *main);
        }
        light.ReferenceDraw = casters.front();
        light.CandidateCount = light.ClusterCount = 1;
        light.GeometryLightWeight = desc.EnvironmentKey->Intensity;
    } else {
        if (desc.NativeDirectStrength <= 0.0F) return false;
        light = ResolvePicaDirectionalShadowLightSelection(casters);
    }
    if (!light.Valid())
        return false;
    mImpl->LastLight = { light.WorldDirectionTowardSource, light.CandidateCount, light.ClusterCount,
                         light.GeometryLightWeight };
    mImpl->LastStage = NriDirectionalShadowExecutionStage::NativeLightResolved;

    // Use the exact camera captured with this draw, for both authored and
    // native lights. Later gameplay camera samples cannot move existing pages.
    DirectionalShadowMatrix viewToWorld;
    if (!InvertDirectionalShadowMatrix(*light.ReferenceDraw->LightingWorldToView, viewToWorld))
        return false;
    DirectionalShadowFrameInput input;
    input.CameraWorld = { viewToWorld[12], viewToWorld[13], viewToWorld[14] };
    input.LightDirectionTowardSource = light.WorldDirectionTowardSource;
    input.Settings = desc.Settings;
    input.CullCasters = desc.Environment == nullptr || desc.Environment->ShadowCasterCulling;
    // Validates the settings and the light. Each layout is planned at its own
    // direction below.
    const auto plan = BuildDirectionalShadowFramePlan(input);
    if (!plan.Ready())
        return false;
    const Renderer3ds::ShadowVector lightDirection = plan.LightDirectionTowardSource;
    mImpl->LastLight.WorldDirectionTowardSource = lightDirection;
    mImpl->LastStage = NriDirectionalShadowExecutionStage::FramePlanReady;
    // A caster whose program cannot be built casts nothing; it never fails
    // the map for the others. A discarding alpha test without its program
    // never becomes a solid caster.
    std::array<nri::Pipeline*, kMaximumCasters> pipelines{};
    size_t drawableCount = 0;
    for (size_t index = 0; index < casterCount; ++index) {
        const auto& material = casterStorage[index]->Material;
        if (material.AlphaTestEnabled && material.AlphaCompare != PicaCompareFunction::Always &&
            !UsesAlphaCasterProgram(*casterStorage[index]))
            continue;
        auto* pipeline = mImpl->GetOrCreatePipeline(*casterStorage[index], plan);
        if (pipeline == nullptr)
            continue;
        pipelines[drawableCount] = pipeline;
        casterStorage[drawableCount++] = casterStorage[index];
    }
    casterCount = drawableCount;
    const auto drawable = std::span(casterStorage).first(casterCount);
    auto* target = mImpl->EnsureTarget({ desc.RenderTargetNamespace, desc.ColorPhysicalAddress },
                                       plan.Virtual.AtlasResolution, desc.FrameId);
    if (target == nullptr)
        return false;
    auto& history = target->Slot;
    history.State = {};
    mImpl->LastStage = NriDirectionalShadowExecutionStage::ResourcesReady;
    std::array<Renderer3ds::VirtualShadowCaster, kMaximumCasters> geometry{};
    for (size_t index = 0; index < casterCount; ++index) {
        geometry[index] = drawable[index]->ShadowCaster;
        if (geometry[index].ContentVersion != 0)
            geometry[index].ContentVersion =
                HashValue(geometry[index].ContentVersion, mImpl->PipelineKey(*drawable[index], plan));
    }
    const auto casterGeometry = std::span<const Renderer3ds::VirtualShadowCaster>(geometry).first(casterCount);

    // Shadow keyframes: which layouts are displayed, which one is constructed
    // and with what page budget. The scene's continuity epoch marks its cuts.
    auto& keyframes = target->Keyframes;
    const int32_t previousNewer = keyframes.Frame().Newer;
    const uint32_t layoutPages =
        previousNewer >= 0 ? target->Layouts[previousNewer].Cache.Statistics().Resident : 0U;
    const uint64_t layoutKey =
        HashValue(HashValue(1469598103934665603ULL, plan.Virtual.AtlasResolution), plan.Virtual.MaximumDistance);
    auto schedule =
        keyframes.Begin(lightDirection, scene != nullptr ? scene->ContinuityEpoch : 0U, layoutKey, layoutPages);
    const bool cut = schedule.Cut;
    // A still light on slot 0 needs one region and one layout; a light that
    // moves needs a region per slot. After a while without motion the other
    // layouts are released.
    const bool still = schedule.Construction < 0 && !schedule.OlderDisplayed();
    target->StillFrames = still ? std::min(target->StillFrames + 1U, kStillLayoutReleaseFrames) : 0U;
    const bool release = target->StillFrames == kStillLayoutReleaseFrames;
    if (release)
        for (int32_t slotIndex = 0; slotIndex < Renderer3ds::kVirtualShadowKeyframeSlots; ++slotIndex) {
            auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
            if (slotIndex == schedule.Newer || slot.StaticTexture.Image == VK_NULL_HANDLE)
                continue;
            mImpl->RetireTexture(slot.StaticTexture, desc.FrameId);
            slot = {};
        }
    uint32_t regions = target->Regions;
    if (std::max(schedule.Newer, schedule.Construction) > 0 || !still)
        regions = static_cast<uint32_t>(Renderer3ds::kVirtualShadowKeyframeSlots);
    else if (release && schedule.Newer == 0)
        regions = 1U;
    bool atlasReplaced = false;
    if (!mImpl->ResizeAtlas(*target, regions, desc.FrameId, atlasReplaced))
        return false;
    if (atlasReplaced)
        for (auto& slot : target->Layouts)
            slot.Cache.InvalidateComposition();
    const auto regionOffset = [&](int32_t slotIndex) {
        return target->Regions == 1U ? 0U : static_cast<uint32_t>(slotIndex) * target->Resolution;
    };

    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, desc.FrameSlot);
    auto& frame = mImpl->Frames[desc.FrameSlot % kFrameSlots];
    if (command == nullptr || frame.CasterPool == nullptr || frame.ShadowUniformMapped == nullptr)
        return false;
    if (frame.FrameId != desc.FrameId) {
        for (auto* view : frame.TransientViews)
            mImpl->Core->DestroyDescriptor(view);
        frame.TransientViews.clear();
        frame.FrameId = desc.FrameId;
        frame.CastersUsed = 0;
        frame.ContactUsed = 0;
    }
    using PreparedCasters = std::array<Impl::PreparedCaster, kMaximumCasters>;
    std::array<PreparedCasters, Renderer3ds::kVirtualShadowKeyframeSlots> prepared{};
    Renderer3ds::VirtualShadowPageStatistics pages;
    uint32_t updatedPages = 0U;
    // Plans a layout at its own light direction and the current camera,
    // selects its pages and binds the casters its updates draw. A
    // construction draws static pages only, within its budget.
    const auto prepareLayout = [&](int32_t slotIndex, const Renderer3ds::VirtualShadowConstruction* construction) {
        auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
        if (slot.StaticTexture.Image == VK_NULL_HANDLE &&
            !mImpl->CreateAtlas(target->Resolution, target->Resolution, false, slot.StaticTexture))
            return false;
        DirectionalShadowFrameInput slotInput = input;
        slotInput.LightDirectionTowardSource = keyframes.Direction(slotIndex);
        slotInput.PreviousLayout = slot.Layout.Valid() ? &slot.Layout : nullptr;
        const auto slotPlan = BuildDirectionalShadowFramePlan(slotInput);
        if (!slotPlan.Ready())
            return false;
        slot.Layout = slotPlan.Virtual;
        slot.Cache.Prepare(slot.Layout, casterGeometry, desc.FrameId, construction);
        const auto& statistics = slot.Cache.Statistics();
        pages.Requested += statistics.Requested;
        pages.Resident += statistics.Resident;
        pages.Reused += statistics.Reused;
        pages.Empty += statistics.Empty;
        pages.Evicted += statistics.Evicted;
        pages.Unmapped += statistics.Unmapped;
        pages.StaticUpdated += statistics.StaticUpdated;
        pages.StaticReused += statistics.StaticReused;
        pages.Pending += statistics.Pending;
        updatedPages += static_cast<uint32_t>(slot.Cache.Updates().size());
        std::array<bool, kMaximumCasters> required{};
        for (const auto& page : slot.Cache.Updates()) {
            for (auto index : slot.Cache.StaticCasters(page))
                required[index] = true;
            for (auto index : slot.Cache.DynamicCasters(page))
                required[index] = true;
        }
        for (size_t index = 0; index < casterCount; ++index) {
            if (!required[index])
                continue;
            auto& draw = prepared[static_cast<size_t>(slotIndex)][index];
            draw = {};
            draw.Pipeline = pipelines[index];
            if (draw.Pipeline == nullptr ||
                !mImpl->BindCaster(frame, sceneFrame, *drawable[index], slot.Layout.WorldToLight, draw))
                return false;
        }
        return true;
    };

    mImpl->LastShadowMapBarrierExecution = shadowMapBarrierPlan.BeginExecution();
    const auto transition = [&](VkImage image, ResourceAccess access) {
        const auto change = mImpl->OwnedStates.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, desc.FrameSlot, image, change))
            return false;
        mImpl->LastShadowMapBarrierExecution.RecordGraphTransition(change);
        mImpl->OwnedStates.Commit(change);
        return true;
    };
    constexpr uint32_t pageSize = Renderer3ds::kVirtualShadowPhysicalPageTexels;
    const uint32_t pagesAcross = target->Resolution / pageSize;
    // Draws the pages of one layout: its static pages into its static atlas,
    // or its dynamic casters into its region of the composed atlas.
    const auto rasterize = [&](VkImage image, int32_t slotIndex, bool staticLayer) {
        const uint32_t offsetX = staticLayer ? 0U : regionOffset(slotIndex);
        const auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
        const auto& draws = prepared[static_cast<size_t>(slotIndex)];
        nri::RenderingDesc rendering{};
        rendering.depth.descriptor = NriInteropAccess::DepthAttachmentView(*mImpl->Interop, image);
        if (rendering.depth.descriptor == nullptr)
            return false;
        rendering.depth.loadOp = nri::LoadOp::LOAD;
        rendering.depth.storeOp = nri::StoreOp::STORE;
        mImpl->Core->CmdSetDescriptorPool(*command, *frame.CasterPool);
        mImpl->Core->CmdBeginRendering(*command, rendering);
        mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::GRAPHICS, *mImpl->ShadowLayout);
        nri::Pipeline* boundPipeline = nullptr;
        for (const auto& page : slot.Cache.Updates()) {
            if (staticLayer ? !page.RefreshStatic : page.DynamicCasterCount == 0)
                continue;
            const uint32_t x = offsetX + (page.PhysicalPage % pagesAcross) * pageSize;
            const uint32_t y = (page.PhysicalPage / pagesAcross) * pageSize;
            const nri::Rect scissor{ static_cast<int32_t>(x), static_cast<int32_t>(y), pageSize, pageSize };
            if (staticLayer) {
                nri::ClearAttachmentDesc clear{};
                clear.planes = nri::PlaneBits::DEPTH;
                clear.value.depthStencil.depth = 1;
                mImpl->Core->CmdClearAttachments(*command, &clear, 1, &scissor, 1);
            }
            const nri::Viewport viewport{ float(x), float(y), float(pageSize), float(pageSize), 0, 1, true };
            mImpl->Core->CmdSetViewports(*command, &viewport, 1);
            mImpl->Core->CmdSetScissors(*command, &scissor, 1);
            constexpr float coverage = float(Renderer3ds::kVirtualShadowPageTexels) / pageSize;
            const float scale = 2.0F * coverage / slot.Layout.Levels[page.Key.Level].PageWorldSize;
            const std::array<float, 4> pageTransform{ scale, scale, (-2.0F * float(page.Key.X) - 1.0F) * coverage,
                                                      (-2.0F * float(page.Key.Y) - 1.0F) * coverage };
            const nri::SetRootConstantsDesc constants{ 0, pageTransform.data(), sizeof(pageTransform) };
            mImpl->Core->CmdSetRootConstants(*command, constants);
            const auto indices = staticLayer ? slot.Cache.StaticCasters(page) : slot.Cache.DynamicCasters(page);
            for (auto index : indices) {
                const auto& caster = *drawable[index];
                const auto& draw = draws[index];
                if (boundPipeline != draw.Pipeline) {
                    mImpl->Core->CmdSetPipeline(*command, *draw.Pipeline);
                    boundPipeline = draw.Pipeline;
                }
                const nri::SetDescriptorSetDesc set{ 0, draw.Set, nri::BindPoint::GRAPHICS };
                mImpl->Core->CmdSetDescriptorSet(*command, set);
                for (const auto& binding : sceneFrame.VertexBindings(caster)) {
                    const nri::VertexBufferDesc vertex{ draw.Geometry, binding.Offset, binding.Stride };
                    mImpl->Core->CmdSetVertexBuffers(*command, binding.Binding, &vertex, 1);
                }
                if (caster.Indexed) {
                    mImpl->Core->CmdSetIndexBuffer(*command, *draw.Geometry, caster.IndexOffset,
                                                   nri::IndexType::UINT16);
                    const nri::DrawIndexedDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, caster.BaseVertex, 0 };
                    mImpl->Core->CmdDrawIndexed(*command, nativeDraw);
                } else {
                    const nri::DrawDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, 0 };
                    mImpl->Core->CmdDraw(*command, nativeDraw);
                }
                ++mImpl->LastCasters;
                if (UsesAlphaCasterProgram(caster))
                    ++mImpl->LastAlphaCasters;
            }
        }
        mImpl->Core->CmdEndRendering(*command);
        return true;
    };

    // Restores the changed pages of a layout from its static depth into its
    // region of the composed atlas (in TransferWrite). Unchanged pages remain
    // untouched and retain their exact depth.
    const auto restore = [&](int32_t slotIndex) {
        auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
        auto* source = NriInteropAccess::Texture(*mImpl->Interop, slot.StaticTexture.Image);
        auto* destination = NriInteropAccess::Texture(*mImpl->Interop, history.Texture.Image);
        if (source == nullptr || destination == nullptr ||
            !transition(slot.StaticTexture.Image, staticBarrier->CompletionAccess))
            return false;
        for (const auto& page : slot.Cache.Updates()) {
            nri::TextureRegionDesc from{};
            from.x = static_cast<nri::Dim_t>((page.PhysicalPage % pagesAcross) * pageSize);
            from.y = static_cast<nri::Dim_t>((page.PhysicalPage / pagesAcross) * pageSize);
            from.width = from.height = pageSize;
            from.depth = 1;
            from.planes = nri::PlaneBits::DEPTH;
            nri::TextureRegionDesc to = from;
            to.x = static_cast<nri::Dim_t>(from.x + regionOffset(slotIndex));
            mImpl->Core->CmdCopyTexture(*command, *destination, &to, *source, &from);
        }
        return true;
    };

    // The layout in construction: static pages only, within the budget,
    // composed as they are drawn so that its display moves no page. They are
    // committed at once: the layout may be displayed (and prepared again) in
    // this same frame.
    auto& telemetry = mImpl->LastKeyframes;
    telemetry.Cut = cut;
    if (schedule.Construction >= 0) {
        const int32_t slotIndex = schedule.Construction;
        const Renderer3ds::VirtualShadowConstruction construction{ schedule.ConstructionBudget };
        if (!prepareLayout(slotIndex, &construction))
            return false;
        auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
        telemetry.ConstructionBudget = schedule.ConstructionBudget;
        telemetry.ConstructionPages = static_cast<uint32_t>(slot.Cache.Updates().size());
        telemetry.PendingPages = slot.Cache.Statistics().Pending;
        if (telemetry.ConstructionPages != 0U &&
            (!transition(slot.StaticTexture.Image, staticBarrier->DispatchAccess) ||
             !rasterize(slot.StaticTexture.Image, slotIndex, true) ||
             !transition(history.Texture.Image, mapBarrier->DispatchAccess) || !restore(slotIndex)))
            return false;
        slot.Cache.Commit();
        schedule = keyframes.EndConstruction(telemetry.PendingPages);
        telemetry.Rotated = schedule.Rotated;
    }

    // The displayed layouts: the newest and, while the dissolve runs, the
    // older one.
    if (schedule.Newer < 0)
        return false;
    const std::array<int32_t, 2> displayed{ schedule.Newer, schedule.OlderDisplayed() ? schedule.Older : -1 };
    bool restored = false;
    bool dynamicDraws = false;
    for (const int32_t slotIndex : displayed) {
        if (slotIndex < 0)
            continue;
        auto& slot = target->Layouts[static_cast<size_t>(slotIndex)];
        if (!prepareLayout(slotIndex, nullptr))
            return false;
        if (slot.Cache.Statistics().StaticUpdated != 0 &&
            (!transition(slot.StaticTexture.Image, staticBarrier->DispatchAccess) ||
             !rasterize(slot.StaticTexture.Image, slotIndex, true)))
            return false;
        restored |= !slot.Cache.Updates().empty();
        for (const auto& page : slot.Cache.Updates())
            dynamicDraws |= page.DynamicCasterCount != 0;
    }
    mImpl->LastLight.Pages = pages;
    mImpl->LastLight.UpdatedPages = updatedPages;
    uint32_t atlases = target->Regions;
    for (const auto& slot : target->Layouts)
        atlases += slot.StaticTexture.Image != VK_NULL_HANDLE ? 1U : 0U;
    mImpl->LastLight.AtlasBytes = uint64_t(target->Resolution) * target->Resolution * sizeof(float) * atlases;
    if (restored) {
        if (!transition(history.Texture.Image, mapBarrier->DispatchAccess))
            return false;
        // Restore every changed page, even when its last actor has left.
        for (const int32_t slotIndex : displayed)
            if (slotIndex >= 0 && !target->Layouts[static_cast<size_t>(slotIndex)].Cache.Updates().empty() &&
                !restore(slotIndex))
                return false;
        if (dynamicDraws) {
            if (!transition(history.Texture.Image, mapBarrier->IntermediateAccess))
                return false;
            for (const int32_t slotIndex : displayed)
                if (slotIndex >= 0 && !rasterize(history.Texture.Image, slotIndex, false))
                    return false;
        }
    }
    mImpl->LastStage = NriDirectionalShadowExecutionStage::ShadowRasterized;
    if (!transition(history.Texture.Image, mapBarrier->CompletionAccess))
        return false;
    for (const int32_t slotIndex : displayed)
        if (slotIndex >= 0)
            target->Layouts[static_cast<size_t>(slotIndex)].Cache.Commit();

    const auto& newer = target->Layouts[static_cast<size_t>(schedule.Newer)];
    history.State.Virtual = newer.Layout;
    history.State.PageTable = newer.Cache.Table();
    history.State.Region = regionOffset(schedule.Newer) / target->Resolution;
    history.State.AtlasRegions = target->Regions;
    Renderer3ds::ShadowVector displayedDirection = newer.Layout.LightDirection;
    if (displayed[1] >= 0) {
        const auto& older = target->Layouts[static_cast<size_t>(displayed[1])];
        auto olderLayout = std::make_shared<PicaDirectionalShadowOlderLayout>();
        olderLayout->Virtual = older.Layout;
        olderLayout->PageTable = older.Cache.Table();
        olderLayout->Region = regionOffset(displayed[1]) / target->Resolution;
        history.State.Older = std::move(olderLayout);
        history.State.NewerWeight = schedule.NewerWeight;
        float length = 0.0F;
        for (size_t axis = 0; axis < 3U; ++axis) {
            displayedDirection[axis] = older.Layout.LightDirection[axis] +
                (newer.Layout.LightDirection[axis] - older.Layout.LightDirection[axis]) * schedule.NewerWeight;
            length += displayedDirection[axis] * displayedDirection[axis];
        }
        length = std::sqrt(length);
        if (length > 1.0e-6F)
            for (auto& component : displayedDirection)
                component /= length;
    }
    // The receivers' lights are matched against the scene's current light;
    // the layouts follow it.
    history.State.WorldLightDirectionTowardSource = lightDirection;
    history.State.ProducedFrameId = desc.FrameId;
    history.State.Strength = plan.Strength * std::clamp(light.ShadowStrength, 0.0F, 1.0F);
    history.State.DepthBias = std::max(plan.DepthBiasConstant, 0.0F) * 0.5F / (2.0F * plan.Virtual.DepthExtent);
    history.State.PcfRadius = plan.PcfRadius;
    history.State.NormalBias = plan.NormalBias;
    // Contact shadows: a camera depth of the same casters, then Godot's
    // screen space contact shadows toward the light. A failure leaves the
    // receivers on the shadow map alone.
    if (contactBarrier != nullptr && plan.ContactShadows.Enabled && desc.ScreenWidth != 0U &&
        desc.ScreenHeight != 0U) {
        const auto& reference = *light.ReferenceDraw;
        const auto projection =
            reference.NativeTransform.CurrentClipToWorldAvailable && reference.LightingWorldToView
                ? BuildContactShadowProjection(reference.NativeTransform.CurrentClipToWorld,
                                               reference.NativeTransform.CurrentViewToWorldAvailable
                                                   ? reference.NativeTransform.CurrentViewToWorld
                                                   : IdentityDirectionalShadowMatrix())
                : std::nullopt;
        if (projection) {
            // Receivers and casters reach it through the lighting view.
            const auto worldToContactClip =
                MultiplyDirectionalShadowMatrices(*projection, *reference.LightingWorldToView);
            const bool contactRendered = mImpl->RenderContactShadows(desc, *command, frame, sceneFrame, drawable, plan,
                                                                     *target, worldToContactClip, lightDirection,
                                                                     *contactBarrier);
            if (contactRendered) {
                history.State.ContactShadows = true;
                history.State.WorldToContactClip = worldToContactClip;
            }
        }
    }
    target->LastUsedFrameId = desc.FrameId;
    telemetry.DisplayedLayouts = displayed[1] >= 0 ? 2U : 1U;
    telemetry.NewerWeight = history.State.NewerWeight;
    telemetry.DisplayedDirectionTowardSource = displayedDirection;
    telemetry.LagDegrees = static_cast<float>(
        Renderer3ds::VirtualShadowKeyframes::AngleDegrees(displayedDirection, lightDirection));
    mImpl->LastNativeLight = !authoredKey;
    mImpl->LastStage = NriDirectionalShadowExecutionStage::HistoryPublished;
    return true;
#endif
}

bool NriDirectionalShadowPass::ExecutePointMaps(const NriPointShadowExecuteDesc& desc,
                                                const NativeSceneView& sceneView,
                                                const EffectPassBarrierPlan& pointShadowMapBarrierPlan) {
    mImpl->LastPoint = {};
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    (void)sceneView;
    (void)pointShadowMapBarrierPlan;
    return false;
#else
    using namespace ::Fast::Renderer3ds;
    const auto* mapBarrier = pointShadowMapBarrierPlan.Find(EffectResource::PointShadowMap);
    const auto passId =
        ::Fast::Renderer::StableExtensionPassId(PicaExtensionPassName(PicaExtensionPass::PointShadowMap));
    if (!Available() ||
        (!desc.ScheduleAuthorization.Matches(passId, EffectStage::BeforeOpaque, desc.ExpectedSurface) &&
         !desc.ScheduleAuthorization.Matches(passId, EffectStage::BeforeUi, desc.ExpectedSurface)) ||
        desc.ExpectedSurface.Namespace != desc.RenderTargetNamespace ||
        desc.ExpectedSurface.Resource != desc.ColorPhysicalAddress ||
        desc.Settings.Mode != PointShadowMode::CubeMaps || !sceneView.Active() ||
        sceneView.FrameId() != desc.FrameId || sceneView.PicaFrame() == nullptr ||
        !sceneView.PicaFrame()->Active() || sceneView.PicaFrame()->FrameId() != desc.FrameId ||
        sceneView.PicaFrame()->FrameSlot() != desc.FrameSlot || !pointShadowMapBarrierPlan.Valid() ||
        mapBarrier == nullptr || mapBarrier->DispatchAccess != ResourceAccess::DepthAttachment ||
        mapBarrier->CompletionAccess != ResourceAccess::ShaderRead || !mapBarrier->DownstreamRead)
        return false;

    // Casters of this target and the point lights their lighting sees.
    const auto& sceneFrame = *sceneView.PicaFrame();
    std::vector<const PicaSceneDrawRecord*> casters;
    std::vector<nri::Pipeline*> pipelines;
    std::array<PointShadowLight, 64> candidates{};
    size_t candidateCount = 0;
    const auto addCandidate = [&](const PointShadowLight& light) {
        if (light.SourceIdentity == 0U || light.Radius <= 0.0F || candidateCount == candidates.size() ||
            std::any_of(candidates.begin(), candidates.begin() + candidateCount,
                        [&](const auto& existing) { return existing.SourceIdentity == light.SourceIdentity; }))
            return;
        candidates[candidateCount++] = light;
    };
    const auto& environment = desc.Environment;
    // Placed lights that cast shadows (a cube map at their position), while
    // the environment applies them. Suns never do.
    for (uint32_t index = 0; environment.Enabled &&
         index < std::min<uint32_t>(environment.PlacedLightCount, kMaximumPicaPlacedLights); ++index) {
        const auto& placed = environment.PlacedLights[index];
        if (placed.CastShadows && placed.Kind != PicaPlacedLightKind::Sun)
            addCandidate({ placed.SourceIdentity, placed.PositionWorld, placed.Reach,
                           std::max({ placed.Color[0], placed.Color[1], placed.Color[2] }) });
    }
    std::optional<ShadowVector> camera;
    const auto draws = sceneFrame.Draws();
    for (size_t index = 0; index < draws.size(); ++index) {
        const auto& draw = draws[index];
        if (draw.RenderTarget.RenderTargetNamespace != desc.RenderTargetNamespace ||
            draw.RenderTarget.ColorPhysicalAddress != desc.ColorPhysicalAddress || !draw.LightingWorldToView)
            continue;
        DirectionalShadowMatrix viewToWorld;
        if (!camera && InvertDirectionalShadowMatrix(*draw.LightingWorldToView, viewToWorld))
            camera = ShadowVector{ viewToWorld[12], viewToWorld[13], viewToWorld[14] };
        if (draw.ScenePoints) {
            for (uint32_t light = 0; light < std::min<uint32_t>(draw.ScenePoints->Count, kMaximumPicaScenePointLights); ++light) {
                const auto& point = draw.ScenePoints->Lights[light];
                if (point.Group >= environment.PointLights.size()) continue;
                const auto& group = environment.PointLights[point.Group];
                if (!group.Enabled || group.Intensity <= 0.0F || point.Radius <= 0.0F) continue;
                addCandidate({ point.SourceIdentity, point.PositionWorld, point.Radius * group.RadiusScale,
                               std::max({ point.Color[0], point.Color[1], point.Color[2] }) * group.Intensity });
            }
        }
        if (!environment.Enabled || environment.NativeLocalStrength > 0.0F) {
            for (const auto& source : draw.NativeLightSources) {
                if (source.Kind != PicaNativeLightSource::Type::Point || source.Radius <= 0.0F) continue;
                const bool scenePoint = draw.ScenePoints &&
                    std::any_of(draw.ScenePoints->Lights.begin(), draw.ScenePoints->Lights.begin() + draw.ScenePoints->Count,
                                [&](const auto& point) { return point.SourceIdentity == source.SourceIdentity; });
                if (!scenePoint)
                    addCandidate({ source.SourceIdentity, source.PositionWorld, source.Radius,
                                   std::max({ source.UnattenuatedColor[0], source.UnattenuatedColor[1],
                                              source.UnattenuatedColor[2] }) });
            }
        }
        if (!sceneFrame.DirectionalShadowCaster(index) || !draw.NativeTransform.CurrentViewToWorldAvailable ||
            !draw.NativeTransform.CurrentClipToWorldAvailable)
            continue;
        const auto& material = draw.Material;
        if (material.AlphaTestEnabled && material.AlphaCompare != PicaCompareFunction::Always &&
            !UsesAlphaCasterProgram(draw))
            continue;
        auto* pipeline = mImpl->GetOrCreatePointPipeline(draw);
        if (pipeline == nullptr || casters.size() == kMaximumCasters)
            continue;
        casters.push_back(&draw);
        pipelines.push_back(pipeline);
    }
    mImpl->LastPoint.CandidateLights = static_cast<uint32_t>(candidateCount);
    const auto layout = BuildPointShadowLayout(desc.Settings.Resolution, desc.Settings.MaxLights);
    if (!camera || !layout.Valid())
        return false;
    auto* target = mImpl->EnsurePointTarget({ desc.RenderTargetNamespace, desc.ColorPhysicalAddress }, layout, desc.FrameId);
    if (target == nullptr)
        return false;
    const bool fresh = target->ProducedFrameId == 0U;
    const auto slots = SelectPointShadowSlots(std::span(candidates).first(candidateCount), *camera,
                                              desc.Settings.Range, layout.SlotCount, target->Slots);

    // A row is re-rendered unless its light, reach and static casters are
    // unchanged; an actor or an unproven caster in reach renders it again.
    std::array<bool, kMaximumPointShadowLights> render{};
    std::array<uint64_t, kMaximumPointShadowLights> signatures{};
    for (uint32_t row = 0; row < layout.SlotCount; ++row) {
        const auto& slot = slots[row];
        if (!slot.Valid()) continue;
        ++mImpl->LastPoint.Lights;
        uint64_t signature = HashValue(1469598103934665603ULL, slot);
        bool dynamic = false;
        for (const auto* caster : casters) {
            const auto& bounds = caster->ShadowCaster.WorldBounds;
            bool reached = false;
            for (uint32_t face = 0; face < kPointShadowFaceCount && !reached; ++face)
                reached = PointShadowFaceIntersects(slot, face, layout, bounds);
            if (!reached) continue;
            dynamic |= caster->ShadowCaster.Dynamic || caster->ShadowCaster.ContentVersion == 0U;
            signature = HashValue(signature, caster->ShadowCaster.ContentVersion);
        }
        signatures[row] = dynamic ? 0U : signature;
        render[row] = fresh || dynamic || signatures[row] != target->Signatures[row];
    }

    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, desc.FrameSlot);
    auto& frame = mImpl->Frames[desc.FrameSlot % kFrameSlots];
    if (command == nullptr || frame.CasterPool == nullptr || frame.ShadowUniformMapped == nullptr)
        return false;
    if (frame.FrameId != desc.FrameId) {
        for (auto* view : frame.TransientViews)
            mImpl->Core->DestroyDescriptor(view);
        frame.TransientViews.clear();
        frame.FrameId = desc.FrameId;
        frame.CastersUsed = 0;
        frame.ContactUsed = 0;
    }
    const auto transition = [&](VkImage image, ResourceAccess access) {
        const auto change = mImpl->OwnedStates.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, desc.FrameSlot, image, change))
            return false;
        mImpl->OwnedStates.Commit(change);
        return true;
    };
    if (!transition(target->Texture.Image, mapBarrier->DispatchAccess))
        return false;
    nri::RenderingDesc rendering{};
    rendering.depth.descriptor = NriInteropAccess::DepthAttachmentView(*mImpl->Interop, target->Texture.Image);
    if (rendering.depth.descriptor == nullptr)
        return false;
    rendering.depth.loadOp = nri::LoadOp::LOAD;
    rendering.depth.storeOp = nri::StoreOp::STORE;
    mImpl->Core->CmdSetDescriptorPool(*command, *frame.CasterPool);
    mImpl->Core->CmdBeginRendering(*command, rendering);
    mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::GRAPHICS, *mImpl->ShadowLayout);
    const uint32_t tile = layout.TileTexels;
    if (fresh) {
        // A new atlas holds no depth yet: every row reads as unoccluded.
        nri::ClearAttachmentDesc clear{};
        clear.planes = nri::PlaneBits::DEPTH;
        clear.value.depthStencil.depth = 1;
        const nri::Rect whole{ 0, 0, static_cast<nri::Dim_t>(layout.Width), static_cast<nri::Dim_t>(layout.Height) };
        mImpl->Core->CmdClearAttachments(*command, &clear, 1, &whole, 1);
    }
    std::vector<Impl::PreparedCaster> prepared(casters.size());
    std::vector<bool> bound(casters.size(), false);
    nri::Pipeline* boundPipeline = nullptr;
    bool failed = false;
    for (uint32_t row = 0; row < layout.SlotCount && !failed; ++row) {
        if (!render[row]) continue;
        const auto& slot = slots[row];
        ++mImpl->LastPoint.RenderedLights;
        nri::ClearAttachmentDesc clear{};
        clear.planes = nri::PlaneBits::DEPTH;
        clear.value.depthStencil.depth = 1;
        const nri::Rect rowRect{ 0, static_cast<int32_t>(row * tile), static_cast<nri::Dim_t>(layout.Width),
                                 static_cast<nri::Dim_t>(tile) };
        mImpl->Core->CmdClearAttachments(*command, &clear, 1, &rowRect, 1);
        for (uint32_t face = 0; face < kPointShadowFaceCount && !failed; ++face) {
            const nri::Rect scissor{ static_cast<int32_t>(face * tile), static_cast<int32_t>(row * tile),
                                     static_cast<nri::Dim_t>(tile), static_cast<nri::Dim_t>(tile) };
            const nri::Viewport viewport{ float(face * tile), float(row * tile), float(tile), float(tile), 0, 1, true };
            mImpl->Core->CmdSetViewports(*command, &viewport, 1);
            mImpl->Core->CmdSetScissors(*command, &scissor, 1);
            const auto faceMatrix = BuildPointShadowFaceMatrix(slot, face, layout);
            const nri::SetRootConstantsDesc constants{ 0, faceMatrix.data(), sizeof(faceMatrix) };
            mImpl->Core->CmdSetRootConstants(*command, constants);
            for (size_t index = 0; index < casters.size(); ++index) {
                const auto& caster = *casters[index];
                if (!PointShadowFaceIntersects(slot, face, layout, caster.ShadowCaster.WorldBounds)) continue;
                auto& draw = prepared[index];
                if (!bound[index]) {
                    draw.Pipeline = pipelines[index];
                    if (!mImpl->BindCaster(frame, sceneFrame, caster, IdentityDirectionalShadowMatrix(), draw)) {
                        failed = true;
                        break;
                    }
                    bound[index] = true;
                }
                if (boundPipeline != draw.Pipeline) {
                    mImpl->Core->CmdSetPipeline(*command, *draw.Pipeline);
                    boundPipeline = draw.Pipeline;
                }
                const nri::SetDescriptorSetDesc set{ 0, draw.Set, nri::BindPoint::GRAPHICS };
                mImpl->Core->CmdSetDescriptorSet(*command, set);
                for (const auto& binding : sceneFrame.VertexBindings(caster)) {
                    const nri::VertexBufferDesc vertex{ draw.Geometry, binding.Offset, binding.Stride };
                    mImpl->Core->CmdSetVertexBuffers(*command, binding.Binding, &vertex, 1);
                }
                if (caster.Indexed) {
                    mImpl->Core->CmdSetIndexBuffer(*command, *draw.Geometry, caster.IndexOffset, nri::IndexType::UINT16);
                    const nri::DrawIndexedDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, caster.BaseVertex, 0 };
                    mImpl->Core->CmdDrawIndexed(*command, nativeDraw);
                } else {
                    const nri::DrawDesc nativeDraw{ caster.VertexOrIndexCount, 1, 0, 0 };
                    mImpl->Core->CmdDraw(*command, nativeDraw);
                }
                ++mImpl->LastPoint.CasterDraws;
                if (UsesAlphaCasterProgram(caster))
                    ++mImpl->LastPoint.AlphaCasterDraws;
            }
        }
    }
    mImpl->Core->CmdEndRendering(*command);
    if (!transition(target->Texture.Image, mapBarrier->CompletionAccess) || failed) {
        target->ProducedFrameId = 0U;
        target->Signatures.fill(0U);
        return false;
    }
    target->Slots = slots;
    target->Signatures = signatures;
    target->Range = desc.Settings.Range;
    target->Strength = desc.Settings.Strength;
    target->ProducedFrameId = desc.FrameId;
    target->LastUsedFrameId = desc.FrameId;
    mImpl->LastPoint.AtlasBytes = uint64_t(layout.Width) * layout.Height * sizeof(float);
    return true;
#endif
}

NriPointShadowHistorySnapshot NriDirectionalShadowPass::FindLatestPointHistory(
    uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress, uint64_t atFrameId) const noexcept {
    NriPointShadowHistorySnapshot result;
    const auto found = mImpl->PointTargets.find({ renderTargetNamespace, colorPhysicalAddress });
    if (!Available() || atFrameId == 0U || found == mImpl->PointTargets.end())
        return result;
    const auto& target = found->second;
    if (target.ProducedFrameId == 0U || target.ProducedFrameId > atFrameId)
        return result;
    result.Image = target.Texture.Image;
    result.Layout = target.Layout;
    result.Slots = target.Slots;
    result.Range = target.Range;
    result.Strength = target.Strength;
    result.ProducedFrameId = target.ProducedFrameId;
    return result.Valid() ? result : NriPointShadowHistorySnapshot{};
}

const NriPointShadowTelemetry& NriDirectionalShadowPass::LastPointTelemetry() const {
    return mImpl->LastPoint;
}

NriDirectionalShadowHistorySnapshot NriDirectionalShadowPass::FindLatestHistory(uint64_t renderTargetNamespace,
                                                                                uint32_t colorPhysicalAddress,
                                                                                uint64_t atFrameId) const noexcept {
    NriDirectionalShadowHistorySnapshot result;
    if (!Available() || atFrameId == 0)
        return result;
    const auto found = mImpl->Histories.find({ renderTargetNamespace, colorPhysicalAddress });
    if (found == mImpl->Histories.end())
        return result;
    const auto& slot = found->second.Slot;
    if (!slot.State.Valid() || slot.State.ProducedFrameId > atFrameId)
        return result;
    result.Image = slot.Texture.Image;
    result.View = slot.Texture.SampledView;
    result.Sampler = mImpl->ReceiverSampler;
    result.State = slot.State;
    // The contact shadows are screen space: only the frame that produced
    // them may read them. A skipped or disabled production leaves the
    // receivers on the shadow map alone.
    const auto& contact = found->second.Contact;
    result.State.ContactShadows = slot.State.ContactShadows && slot.State.ProducedFrameId == atFrameId;
    if (result.State.ContactShadows) {
        result.ContactImage = contact.Visibility.Image;
        result.ContactWidth = contact.Width;
        result.ContactHeight = contact.Height;
    }
    return result.Valid() ? result : NriDirectionalShadowHistorySnapshot{};
}

void NriDirectionalShadowPass::InvalidateHistory() noexcept {
    for (auto& [key, history] : mImpl->Histories) {
        history.Slot.State = {};
        for (auto& layout : history.Layouts)
            layout.Cache.Invalidate();
        // The next map is a discontinuity: one layout, rebuilt at once.
        history.Keyframes.Reset();
        history.StillFrames = 0U;
    }
    for (auto& [key, point] : mImpl->PointTargets) {
        (void)key;
        point.ProducedFrameId = 0U;
        point.Signatures.fill(0U);
    }
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
            if (frame.ShadowUniformMapped != nullptr && frame.ShadowUniformBuffer != nullptr) {
                mImpl->Core->UnmapBuffer(*frame.ShadowUniformBuffer);
            }
            for (nri::Descriptor* view : frame.ShadowUniformViews) {
                if (view != nullptr) {
                    mImpl->Core->DestroyDescriptor(view);
                }
            }
            if (frame.ShadowUniformBuffer != nullptr) {
                mImpl->Core->DestroyBuffer(frame.ShadowUniformBuffer);
            }
            if (frame.CasterPool != nullptr) {
                mImpl->Core->DestroyDescriptorPool(frame.CasterPool);
            }
            if (frame.ContactPool != nullptr) {
                mImpl->Core->DestroyDescriptorPool(frame.ContactPool);
            }
        }
        for (auto& [key, pipeline] : mImpl->ShadowPipelines) {
            (void)key;
            mImpl->Core->DestroyPipeline(pipeline);
        }
        for (auto& [key, pipeline] : mImpl->PointPipelines) {
            (void)key;
            mImpl->Core->DestroyPipeline(pipeline);
        }
        for (auto& [key, sampler] : mImpl->CasterSamplers) {
            (void)key;
            mImpl->Core->DestroyDescriptor(sampler);
        }
        if (mImpl->ShadowLayout != nullptr) {
            mImpl->Core->DestroyPipelineLayout(mImpl->ShadowLayout);
        }
        for (nri::Pipeline* pipeline : mImpl->ContactPipelines)
            if (pipeline != nullptr)
                mImpl->Core->DestroyPipeline(pipeline);
        if (mImpl->ContactDepthSampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->ContactDepthSampler);
        if (mImpl->ContactLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->ContactLayout);
    }
#endif
    if (mImpl->ReceiverSampler != VK_NULL_HANDLE && mImpl->Device != VK_NULL_HANDLE) {
        vkDestroySampler(mImpl->Device, mImpl->ReceiverSampler, nullptr);
    }
    *mImpl = {};
    mImpl->Reason = "NRI directional shadow pass is not initialized";
}

bool NriDirectionalShadowPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr && mImpl->Core != nullptr &&
           mImpl->ShadowLayout != nullptr && mImpl->ReceiverSampler != VK_NULL_HANDLE;
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

uint32_t NriDirectionalShadowPass::LastAlphaCasterCount() const {
    return mImpl->LastAlphaCasters;
}

const NriDirectionalShadowLightTelemetry& NriDirectionalShadowPass::LastLightTelemetry() const {
    return mImpl->LastLight;
}

const NriDirectionalShadowKeyframeTelemetry& NriDirectionalShadowPass::LastKeyframeTelemetry() const {
    return mImpl->LastKeyframes;
}

NriDirectionalShadowExecutionStage NriDirectionalShadowPass::LastExecutionStage() const {
    return mImpl->LastStage;
}

const EffectPassBarrierExecution& NriDirectionalShadowPass::LastShadowMapBarrierExecution() const {
    return mImpl->LastShadowMapBarrierExecution;
}

const std::string& NriDirectionalShadowPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
