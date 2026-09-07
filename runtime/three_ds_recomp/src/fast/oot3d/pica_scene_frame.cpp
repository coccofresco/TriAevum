#include "fast/oot3d/pica_scene_frame.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename T>
void HashValue(uint64_t& hash, T value) noexcept {
    for (size_t byte = 0; byte < sizeof(value); ++byte) {
        hash ^= static_cast<uint8_t>(value & 0xffU);
        hash *= kFnvPrime;
        value = static_cast<T>(value >> 8U);
    }
}

uint64_t HashVertexLayout(
    std::span<const PicaNriPackedVertexBinding> bindings,
    std::span<const PicaNriPackedVertexAttribute> attributes) noexcept {
    uint64_t hash = kFnvOffset;
    HashValue(hash, static_cast<uint64_t>(bindings.size()));
    for (const auto& binding : bindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, static_cast<uint8_t>(binding.PerInstance));
    }
    HashValue(hash, static_cast<uint64_t>(attributes.size()));
    for (const auto& attribute : attributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.ByteOffset);
        HashValue(hash, static_cast<uint8_t>(attribute.SourceScalar));
        HashValue(hash, attribute.SourceComponentCount);
        HashValue(hash, attribute.SourceByteOffset);
    }
    return hash;
}

bool SameVertexLayout(
    const PicaSceneVertexLayout& layout,
    std::span<const PicaNriPackedVertexBinding> bindings,
    std::span<const PicaNriPackedVertexAttribute> attributes) noexcept {
    if (layout.Bindings.size() != bindings.size() ||
        layout.Attributes.size() != attributes.size()) {
        return false;
    }
    for (size_t index = 0; index < bindings.size(); ++index) {
        const auto& left = layout.Bindings[index];
        const auto& right = bindings[index];
        if (left.Binding != right.Binding ||
            left.ByteStride != right.ByteStride ||
            left.PerInstance != right.PerInstance) {
            return false;
        }
    }
    for (size_t index = 0; index < attributes.size(); ++index) {
        const auto& left = layout.Attributes[index];
        const auto& right = attributes[index];
        if (left.Location != right.Location || left.Binding != right.Binding ||
            left.ByteOffset != right.ByteOffset ||
            left.SourceScalar != right.SourceScalar ||
            left.SourceComponentCount != right.SourceComponentCount ||
            left.SourceByteOffset != right.SourceByteOffset) {
            return false;
        }
    }
    return true;
}

struct ShaderIdentity {
    PicaSceneShaderStage Stage = PicaSceneShaderStage::Vertex;
    uint64_t Key = 0;

    bool operator==(const ShaderIdentity&) const = default;
};

struct ShaderIdentityHash {
    size_t operator()(const ShaderIdentity& identity) const noexcept {
        uint64_t hash = kFnvOffset;
        HashValue(hash, static_cast<uint8_t>(identity.Stage));
        HashValue(hash, identity.Key);
        return static_cast<size_t>(hash);
    }
};

} // namespace

struct PicaSceneFrame::Impl {
    struct CachedShader {
        std::unique_ptr<PicaSceneShaderProgram> Program;
        ::Fast::Renderer3ds::PicaShaderSourceIdentity SourceIdentity;
    };
    bool FrameActive = false;
    uint32_t CurrentFrameSlot = 0;
    uint64_t CurrentFrameId = 0;
    NativeFrameTemporalSample TemporalSample;
    std::vector<PicaSceneDrawRecord> DrawRecords;
    std::vector<PicaSceneVertexBufferBinding> DrawVertexBindings;
    std::vector<uint8_t> DirectionalShadowCasterFlags;
    std::unordered_map<ShaderIdentity,
                       CachedShader,
                       ShaderIdentityHash>
        Shaders;
    std::unordered_map<uint64_t, std::vector<std::unique_ptr<PicaSceneVertexLayout>>> VertexLayouts;
    uint32_t DirectionalShadowCasters = 0;
    uint32_t DirectionalShadowNativeLightCasters = 0;
    uint32_t NativeLightingStates = 0;
    uint32_t NativeFragmentLightingStates = 0;
    uint32_t NativeFragmentLightingEnabledDraws = 0;
    uint32_t NativeFogStates = 0;
    uint32_t NativeTransformStates = 0;
    uint32_t PreviousNativeTransformStates = 0;
    uint32_t NativeSkeletonStates = 0;
    uint32_t PreviousNativeSkeletonStates = 0;
    uint32_t RejectedDraws = 0;
    uint32_t BoundTextures = 0;
    size_t VertexLayoutCount = 0;
    std::unordered_set<uint64_t> CanonicalPipelines;
    std::unordered_set<uint64_t> CanonicalFullRegisterStates;

    PicaSceneRecordStatus Reject(PicaSceneRecordStatus status) noexcept {
        ++RejectedDraws;
        return status;
    }

    const PicaSceneShaderProgram* ResolveShader(PicaSceneShaderStage stage,
                                                 uint64_t key,
                                                 std::string_view source,
                                                 ::Fast::Renderer3ds::PicaShaderSourceIdentity sourceIdentity,
                                                 const ::Fast::Renderer3ds::PicaVertexShaderHookLayout*
                                                     vertexHooks,
                                                 PicaSceneRecordStatus& status) {
        const ShaderIdentity identity{stage, key};
        const auto found = Shaders.find(identity);
        if (found != Shaders.end()) {
            const auto& cachedIdentity = found->second.SourceIdentity;
            // Identity-bearing callers own immutable, pre-identified strings.
            // Legacy callers still get exact byte comparison, including key conflicts.
            const bool sameSource = sourceIdentity.Available()
                ? sourceIdentity.Size == source.size() && sourceIdentity == cachedIdentity
                : found->second.Program->Source == source;
            if (!sameSource) {
                status = PicaSceneRecordStatus::ShaderIdentityConflict;
                return nullptr;
            }
            if (stage == PicaSceneShaderStage::Vertex &&
                vertexHooks != nullptr && vertexHooks->ValidFor(source)) {
                if (found->second.Program->VertexHooksAvailable() &&
                    found->second.Program->VertexHooks != *vertexHooks) {
                    status = PicaSceneRecordStatus::ShaderIdentityConflict;
                    return nullptr;
                }
                if (!found->second.Program->VertexHooksAvailable()) {
                    found->second.Program->VertexHooks = *vertexHooks;
                }
            }
            return found->second.Program.get();
        }
        const auto actualIdentity = ::Fast::Renderer3ds::IdentifyPicaShaderSource(source);
        if (sourceIdentity.Available() && sourceIdentity != actualIdentity) {
            status = PicaSceneRecordStatus::ShaderIdentityConflict;
            return nullptr;
        }
        auto shader = std::make_unique<PicaSceneShaderProgram>();
        shader->Stage = stage;
        shader->Key = key;
        shader->Source = source;
        if (stage == PicaSceneShaderStage::Vertex &&
            vertexHooks != nullptr && vertexHooks->ValidFor(source)) {
            shader->VertexHooks = *vertexHooks;
        }
        const auto* result = shader.get();
        Shaders.emplace(identity, CachedShader{std::move(shader), actualIdentity});
        return result;
    }

    const PicaSceneVertexLayout* ResolveVertexLayout(
        std::span<const PicaNriPackedVertexBinding> bindings,
        std::span<const PicaNriPackedVertexAttribute> attributes) {
        const uint64_t key = HashVertexLayout(bindings, attributes);
        auto& bucket = VertexLayouts[key];
        const auto found = std::find_if(
            bucket.begin(), bucket.end(),
            [&](const auto& layout) {
                return SameVertexLayout(*layout, bindings, attributes);
            });
        if (found != bucket.end()) {
            return found->get();
        }
        auto layout = std::make_unique<PicaSceneVertexLayout>();
        layout->StructuralKey = key;
        layout->Bindings.assign(bindings.begin(), bindings.end());
        layout->Attributes.assign(attributes.begin(), attributes.end());
        const auto* result = layout.get();
        bucket.push_back(std::move(layout));
        ++VertexLayoutCount;
        return result;
    }
};

PicaSceneFrame::PicaSceneFrame() : mImpl(std::make_unique<Impl>()) {
}

PicaSceneFrame::~PicaSceneFrame() = default;

void PicaSceneFrame::BeginFrame(uint32_t frameSlot, uint64_t frameId) {
    mImpl->FrameActive = true;
    mImpl->CurrentFrameSlot = frameSlot;
    mImpl->CurrentFrameId = frameId;
    mImpl->TemporalSample = {};
    mImpl->DrawRecords.clear();
    mImpl->DrawVertexBindings.clear();
    mImpl->DirectionalShadowCasterFlags.clear();
    mImpl->DirectionalShadowCasters = 0;
    mImpl->DirectionalShadowNativeLightCasters = 0;
    mImpl->NativeLightingStates = 0;
    mImpl->NativeFragmentLightingStates = 0;
    mImpl->NativeFragmentLightingEnabledDraws = 0;
    mImpl->NativeFogStates = 0;
    mImpl->NativeTransformStates = 0;
    mImpl->PreviousNativeTransformStates = 0;
    mImpl->NativeSkeletonStates = 0;
    mImpl->PreviousNativeSkeletonStates = 0;
    mImpl->RejectedDraws = 0;
    mImpl->BoundTextures = 0;
    mImpl->CanonicalPipelines.clear();
    mImpl->CanonicalFullRegisterStates.clear();
}

bool PicaSceneFrame::SetTemporalSample(NativeFrameTemporalSample sample) noexcept {
    if (!mImpl->FrameActive || !sample.Available()) {
        return false;
    }
    mImpl->TemporalSample = sample;
    return true;
}

PicaSceneRecordStatus PicaSceneFrame::Record(
    const PicaSceneDrawRecordDesc& desc) {
    if (!mImpl->FrameActive) {
        return mImpl->Reject(PicaSceneRecordStatus::FrameNotActive);
    }
    if (desc.VertexShaderSource.empty() ||
        desc.FragmentShaderSource.empty() ||
        desc.EffectiveVertexShaderSource.empty() ||
        desc.EffectiveFragmentShaderSource.empty()) {
        return mImpl->Reject(PicaSceneRecordStatus::IncompleteShader);
    }
    if (desc.VertexLayoutBindings.empty() ||
        desc.VertexLayoutAttributes.empty()) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidVertexLayout);
    }
    if (!desc.Raster.Available()) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidRasterState);
    }
    if (!desc.RenderTarget.Available()) {
        return mImpl->Reject(
            PicaSceneRecordStatus::InvalidRenderTargetState);
    }
    if (!desc.Material.Available()) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidMaterialState);
    }
    if (desc.UniformBuffer.NativeHandle == 0U ||
        desc.VertexUniformSize == 0U ||
        desc.VertexUniformOffset > desc.UniformBuffer.Size ||
        desc.VertexUniformSize >
            desc.UniformBuffer.Size - desc.VertexUniformOffset) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidUniformRange);
    }
    if (desc.PreviousVertexUniformHistoryAvailable &&
        (desc.PreviousVertexUniformSize == 0U || desc.PreviousVertexUniformOffset > desc.UniformBuffer.Size ||
         desc.PreviousVertexUniformSize > desc.UniformBuffer.Size - desc.PreviousVertexUniformOffset ||
         desc.PackedPreviousVertexUniforms.size() != desc.PreviousVertexUniformSize)) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidUniformRange);
    }
    if (desc.FragmentUniformSize != 0U &&
        (desc.FragmentUniformOffset > desc.UniformBuffer.Size ||
         desc.FragmentUniformSize >
             desc.UniformBuffer.Size - desc.FragmentUniformOffset)) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidUniformRange);
    }
    if (desc.GeometryBuffer.NativeHandle == 0U ||
        desc.VertexBindings.empty() || desc.VertexOrIndexCount == 0U) {
        return mImpl->Reject(PicaSceneRecordStatus::InvalidGeometryRange);
    }
    for (const auto& binding : desc.VertexBindings) {
        if (binding.Size == 0U || binding.Stride == 0U ||
            binding.Offset > desc.GeometryBuffer.Size ||
            binding.Size > desc.GeometryBuffer.Size - binding.Offset) {
            return mImpl->Reject(PicaSceneRecordStatus::InvalidGeometryRange);
        }
    }
    if (desc.Indexed) {
        const uint64_t indexBytes =
            static_cast<uint64_t>(desc.VertexOrIndexCount) * sizeof(uint16_t);
        if (desc.IndexOffset > desc.GeometryBuffer.Size ||
            indexBytes > desc.GeometryBuffer.Size - desc.IndexOffset) {
            return mImpl->Reject(PicaSceneRecordStatus::InvalidGeometryRange);
        }
    }
    if (desc.VertexBindings.size() >
            std::numeric_limits<uint32_t>::max() ||
        mImpl->DrawVertexBindings.size() >
            std::numeric_limits<uint32_t>::max() -
                desc.VertexBindings.size()) {
        return mImpl->Reject(PicaSceneRecordStatus::TooManyBindings);
    }
    uint8_t seenTextureSlots = 0U;
    uint8_t textureMask = 0U;
    for (const auto& texture : desc.Textures) {
        if (texture.Slot >= 3U ||
            (seenTextureSlots &
             static_cast<uint8_t>(1U << texture.Slot)) != 0U ||
            texture.NativeImageHandle == 0U || texture.ImageWidth == 0U ||
            texture.ImageHeight == 0U || texture.MipLevels == 0U) {
            return mImpl->Reject(
                PicaSceneRecordStatus::InvalidTextureBinding);
        }
        seenTextureSlots |= static_cast<uint8_t>(1U << texture.Slot);
        if (texture.Bound) {
            textureMask |= static_cast<uint8_t>(1U << texture.Slot);
        }
    }

    PicaSceneRecordStatus status = PicaSceneRecordStatus::Recorded;
    const auto* shader = mImpl->ResolveShader(
        PicaSceneShaderStage::Vertex, desc.VertexShaderKey,
        desc.VertexShaderSource, desc.VertexSourceIdentity, &desc.VertexShaderHooks, status);
    const auto* fragmentShader = mImpl->ResolveShader(
        PicaSceneShaderStage::Fragment, desc.FragmentShaderKey,
        desc.FragmentShaderSource, desc.FragmentSourceIdentity, nullptr, status);
    const auto* effectiveVertexShader = mImpl->ResolveShader(
        PicaSceneShaderStage::Vertex, desc.EffectiveVertexShaderKey,
        desc.EffectiveVertexShaderSource, desc.EffectiveVertexSourceIdentity, nullptr, status);
    const auto* effectiveFragmentShader = mImpl->ResolveShader(
        PicaSceneShaderStage::Fragment,
        desc.EffectiveFragmentShaderKey,
        desc.EffectiveFragmentShaderSource, desc.EffectiveFragmentSourceIdentity, nullptr, status);
    if (shader == nullptr || fragmentShader == nullptr ||
        effectiveVertexShader == nullptr ||
        effectiveFragmentShader == nullptr) {
        return mImpl->Reject(status);
    }
    const auto* layout = mImpl->ResolveVertexLayout(
        desc.VertexLayoutBindings, desc.VertexLayoutAttributes);

    PicaSceneDrawRecord record;
    record.SubmissionId = desc.SubmissionId;
    record.CommandListAddress = desc.CommandListAddress;
    record.CommandListOffsetWords = desc.CommandListOffsetWords;
    record.CompositionDomain = desc.CompositionDomain;
    record.Composition = desc.Composition;
    record.Raster = desc.Raster;
    record.RenderTarget = desc.RenderTarget;
    record.CanonicalDescriptorSchemaVersion =
        desc.CanonicalDescriptorSchemaVersion;
    record.CanonicalVertexProgramId = desc.CanonicalVertexProgramId;
    record.CanonicalFragmentProgramId = desc.CanonicalFragmentProgramId;
    record.CanonicalRasterStateId = desc.CanonicalRasterStateId;
    record.CanonicalPipelineId = desc.CanonicalPipelineId;
    record.CanonicalDynamicStateId = desc.CanonicalDynamicStateId;
    record.CanonicalFullRegisterStateId = desc.CanonicalFullRegisterStateId;
    record.GeometryIdentity = desc.GeometryIdentity;
    record.GeometryContentVersion = desc.GeometryContentVersion;
    record.GeometryIdentityAvailable = desc.GeometryIdentityAvailable;
    record.VertexShader = shader;
    record.FragmentShader = fragmentShader;
    record.EffectiveVertexShader = effectiveVertexShader;
    record.EffectiveFragmentShader = effectiveFragmentShader;
    record.VertexLayout = layout;
    record.Material = desc.Material;
    record.UniformBuffer = desc.UniformBuffer;
    record.VertexUniformOffset = desc.VertexUniformOffset;
    record.VertexUniformSize = desc.VertexUniformSize;
    record.VertexUniformContentVersion =
        HashPicaUniformBytes(desc.PackedVertexUniforms);
    record.PreviousVertexUniformOffset = desc.PreviousVertexUniformOffset;
    record.PreviousVertexUniformSize = desc.PreviousVertexUniformHistoryAvailable ? desc.PreviousVertexUniformSize : 0U;
    record.PreviousVertexUniformContentVersion =
        desc.PreviousVertexUniformHistoryAvailable ? HashPicaUniformBytes(desc.PackedPreviousVertexUniforms) : 0U;
    record.PreviousVertexUniformHistoryAvailable = desc.PreviousVertexUniformHistoryAvailable;
    record.FragmentUniformOffset = desc.FragmentUniformOffset;
    record.FragmentUniformSize = desc.FragmentUniformSize;
    record.FragmentUniformContentVersion =
        HashPicaUniformBytes(desc.PackedFragmentUniforms);
    record.GeometryBuffer = desc.GeometryBuffer;
    record.FirstVertexBinding =
        static_cast<uint32_t>(mImpl->DrawVertexBindings.size());
    record.VertexBindingCount =
        static_cast<uint32_t>(desc.VertexBindings.size());
    record.Indexed = desc.Indexed;
    record.IndexOffset = desc.IndexOffset;
    record.VertexOrIndexCount = desc.VertexOrIndexCount;
    record.BaseVertex = desc.BaseVertex;
    record.BoundTextureMask = textureMask;
    for (const auto& texture : desc.Textures) {
        record.Textures[texture.Slot] = texture;
        if (texture.Bound) {
            ++mImpl->BoundTextures;
        }
    }
    record.PerspectiveProjection = desc.PerspectiveProjection;
    const auto nativeEnvironment = DecodePicaNativeDrawEnvironment(
        desc.PackedVertexUniforms, desc.PackedFragmentUniforms,
        desc.Material.FragmentFeatures);
    record.NativeLighting = nativeEnvironment.Lighting;
    record.NativeFragmentLighting = nativeEnvironment.FragmentLighting;
    record.NativeDepth = nativeEnvironment.Depth;
    record.NativeFog = nativeEnvironment.Fog;
    if (desc.VertexShaderHooks.ValidFor(desc.VertexShaderSource)) {
        const auto nativeVertexState =
            DecodePicaNativeVertexState(desc.PackedVertexUniforms, desc.PackedPreviousVertexUniforms,
                                        desc.PreviousVertexUniformHistoryAvailable, desc.VertexShaderHooks);
        record.NativeTransform = nativeVertexState.Transform;
        record.NativeSkeleton = nativeVertexState.Skeleton;
    }
    if (record.NativeLighting.Available) {
        ++mImpl->NativeLightingStates;
    }
    if (record.NativeFragmentLighting.Available) {
        ++mImpl->NativeFragmentLightingStates;
        if (record.NativeFragmentLighting.Enabled) {
            ++mImpl->NativeFragmentLightingEnabledDraws;
        }
    }
    if (record.NativeFog.Available) {
        ++mImpl->NativeFogStates;
    }
    if (record.NativeTransform.CurrentAvailable) {
        ++mImpl->NativeTransformStates;
    }
    if (record.NativeTransform.PreviousAvailable) {
        ++mImpl->PreviousNativeTransformStates;
    }
    if (record.NativeSkeleton.CurrentAvailable) {
        ++mImpl->NativeSkeletonStates;
    }
    if (record.NativeSkeleton.PreviousAvailable) {
        ++mImpl->PreviousNativeSkeletonStates;
    }
    const bool directionalShadowCaster =
        desc.DirectionalShadowCaster && desc.PerspectiveProjection &&
        record.VertexShader->VertexHooksAvailable() &&
        record.VertexShader->VertexHooks.Has(
            ::Fast::Renderer3ds::PicaVertexShaderSemantic::
                ViewPositionOutput) &&
        record.NativeTransform.CurrentViewToWorldAvailable &&
        record.NativeTransform.CurrentUsesSkeleton;
    if (directionalShadowCaster) {
        ++mImpl->DirectionalShadowCasters;
        if (record.NativeLighting.Available &&
            record.NativeLighting.Enabled &&
            record.NativeLighting.ActiveLightCount != 0U) {
            ++mImpl->DirectionalShadowNativeLightCasters;
        }
    }

    mImpl->DrawVertexBindings.insert(
        mImpl->DrawVertexBindings.end(), desc.VertexBindings.begin(),
        desc.VertexBindings.end());
    mImpl->DrawRecords.push_back(record);
    mImpl->DirectionalShadowCasterFlags.push_back(
        static_cast<uint8_t>(directionalShadowCaster));
    if (record.CanonicalPipelineId != 0U) {
        mImpl->CanonicalPipelines.insert(record.CanonicalPipelineId);
    }
    if (record.CanonicalFullRegisterStateId != 0U) {
        mImpl->CanonicalFullRegisterStates.insert(
            record.CanonicalFullRegisterStateId);
    }
    return PicaSceneRecordStatus::Recorded;
}

void PicaSceneFrame::Reset() {
    mImpl->FrameActive = false;
    mImpl->CurrentFrameSlot = 0;
    mImpl->CurrentFrameId = 0;
    mImpl->TemporalSample = {};
    mImpl->DrawRecords.clear();
    mImpl->DrawVertexBindings.clear();
    mImpl->DirectionalShadowCasterFlags.clear();
    mImpl->Shaders.clear();
    mImpl->VertexLayouts.clear();
    mImpl->DirectionalShadowCasters = 0;
    mImpl->DirectionalShadowNativeLightCasters = 0;
    mImpl->NativeLightingStates = 0;
    mImpl->NativeFragmentLightingStates = 0;
    mImpl->NativeFragmentLightingEnabledDraws = 0;
    mImpl->NativeFogStates = 0;
    mImpl->NativeTransformStates = 0;
    mImpl->PreviousNativeTransformStates = 0;
    mImpl->NativeSkeletonStates = 0;
    mImpl->PreviousNativeSkeletonStates = 0;
    mImpl->RejectedDraws = 0;
    mImpl->BoundTextures = 0;
    mImpl->VertexLayoutCount = 0;
    mImpl->CanonicalPipelines.clear();
    mImpl->CanonicalFullRegisterStates.clear();
}

bool PicaSceneFrame::Active() const noexcept {
    return mImpl->FrameActive;
}

uint32_t PicaSceneFrame::FrameSlot() const noexcept {
    return mImpl->CurrentFrameSlot;
}

uint64_t PicaSceneFrame::FrameId() const noexcept {
    return mImpl->CurrentFrameId;
}

const NativeFrameTemporalSample& PicaSceneFrame::TemporalSample() const noexcept {
    return mImpl->TemporalSample;
}

std::span<const PicaSceneDrawRecord> PicaSceneFrame::Draws() const noexcept {
    return mImpl->DrawRecords;
}

PicaResolvedDrawStreamView
PicaSceneFrame::ResolvedDrawStream() const noexcept {
    return {
        kPicaResolvedDrawStreamSchemaVersion,
        mImpl->CurrentFrameId,
        mImpl->DrawRecords,
        mImpl->DrawVertexBindings,
    };
}

std::span<const PicaSceneVertexBufferBinding>
PicaSceneFrame::VertexBindings(const PicaSceneDrawRecord& draw) const noexcept {
    const size_t offset = draw.FirstVertexBinding;
    const size_t count = draw.VertexBindingCount;
    if (offset > mImpl->DrawVertexBindings.size() ||
        count > mImpl->DrawVertexBindings.size() - offset) {
        return {};
    }
    return std::span(mImpl->DrawVertexBindings).subspan(offset, count);
}

bool PicaSceneFrame::DirectionalShadowCaster(
    size_t drawIndex) const noexcept {
    return drawIndex < mImpl->DirectionalShadowCasterFlags.size() &&
           mImpl->DirectionalShadowCasterFlags[drawIndex] != 0U;
}

PicaSceneFrameStats PicaSceneFrame::Stats() const noexcept {
    PicaSceneFrameStats stats;
    stats.DrawCount = static_cast<uint32_t>(std::min<size_t>(
        mImpl->DrawRecords.size(),
        std::numeric_limits<uint32_t>::max()));
    if (mImpl->TemporalSample.Available()) {
        stats.TemporalSampleCount = 1U;
        stats.SyntheticTemporalSampleCount = mImpl->TemporalSample.Synthetic ? 1U : 0U;
        stats.TemporalSampleMultiplier = mImpl->TemporalSample.FixedSampleMultiplier;
        stats.TemporalSampleOrdinal = mImpl->TemporalSample.SampleOrdinal;
    }
    for (const auto& draw : mImpl->DrawRecords) {
        switch (draw.CompositionDomain) {
        case ::Fast::Renderer3ds::PicaCompositionDomain::Scene:
            ++stats.SceneDomainDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionDomain::Ui:
            ++stats.UiDomainDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionDomain::Unknown:
            ++stats.UnknownDomainDrawCount;
            break;
        }
        switch (draw.Composition.Layer) {
        case ::Fast::Renderer3ds::PicaCompositionLayer::OpaqueWorld:
            ++stats.OpaqueWorldLayerDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionLayer::TransparentWorld:
            ++stats.TransparentWorldLayerDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionLayer::Atmosphere:
            ++stats.AtmosphereLayerDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionLayer::Ui:
            ++stats.UiLayerDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionLayer::Unknown:
            ++stats.UnknownLayerDrawCount;
            break;
        }
        switch (draw.Composition.Provenance) {
        case ::Fast::Renderer3ds::PicaCompositionProvenance::NativeCmbDrawPass:
            ++stats.NativeCmbPassProvenanceDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionProvenance::NativeControlFlow:
            ++stats.NativeControlFlowProvenanceDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionProvenance::NativeUiLifecycle:
            ++stats.NativeUiLifecycleProvenanceDrawCount;
            break;
        case ::Fast::Renderer3ds::PicaCompositionProvenance::Unknown:
            ++stats.UnknownProvenanceDrawCount;
            break;
        }
        if (draw.Raster.Available()) {
            ++stats.ResolvedRasterStateCount;
        }
        if (draw.RenderTarget.Available()) {
            ++stats.ResolvedRenderTargetStateCount;
        }
        if (draw.RenderTarget.GpuResourcesAvailable()) {
            ++stats.ResolvedRenderTargetGpuResourceCount;
        }
        if (draw.Material.Available()) {
            ++stats.ResolvedMaterialStateCount;
        }
    }
    stats.DirectionalShadowCasterCount = mImpl->DirectionalShadowCasters;
    stats.DirectionalShadowNativeLightCasterCount =
        mImpl->DirectionalShadowNativeLightCasters;
    stats.NativeLightingStateCount = mImpl->NativeLightingStates;
    stats.NativeFragmentLightingStateCount =
        mImpl->NativeFragmentLightingStates;
    stats.NativeFragmentLightingEnabledDrawCount =
        mImpl->NativeFragmentLightingEnabledDraws;
    stats.NativeFogStateCount = mImpl->NativeFogStates;
    stats.NativeTransformStateCount = mImpl->NativeTransformStates;
    stats.PreviousNativeTransformStateCount =
        mImpl->PreviousNativeTransformStates;
    stats.NativeSkeletonStateCount = mImpl->NativeSkeletonStates;
    stats.PreviousNativeSkeletonStateCount =
        mImpl->PreviousNativeSkeletonStates;
    stats.RejectedDrawCount = mImpl->RejectedDraws;
    stats.BoundTextureCount = mImpl->BoundTextures;
    stats.ShaderCatalogEntries = mImpl->Shaders.size();
    stats.VertexLayoutCatalogEntries = mImpl->VertexLayoutCount;
    stats.CanonicalPipelineEntries = mImpl->CanonicalPipelines.size();
    stats.CanonicalFullRegisterStateEntries =
        mImpl->CanonicalFullRegisterStates.size();
    return stats;
}

} // namespace Fast::Oot3d
