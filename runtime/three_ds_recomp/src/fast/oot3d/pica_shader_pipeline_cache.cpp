#include "fast/oot3d/pica_shader_pipeline_cache.h"

#include "fast/oot3d/pica_rigid_motion.h"

#include <bit>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

void HashCombine(size_t& seed, size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) +
            (seed >> 2U);
}

struct CanonicalKey {
    uint64_t VertexShaderKey = 0;
    uint64_t FragmentShaderKey = 0;
    ::Oot3d::Renderer::PicaShaderSourceIdentity VertexSource;
    ::Oot3d::Renderer::PicaShaderSourceIdentity FragmentSource;

    bool operator==(const CanonicalKey&) const = default;
};

struct CanonicalKeyHash {
    size_t operator()(const CanonicalKey& key) const noexcept {
        size_t result = std::hash<uint64_t>{}(key.VertexShaderKey);
        HashCombine(result, std::hash<uint64_t>{}(key.FragmentShaderKey));
        HashCombine(result, std::hash<uint64_t>{}(key.VertexSource.Id));
        HashCombine(result,
                    std::hash<uint64_t>{}(key.VertexSource.SecondaryHash));
        HashCombine(result, key.VertexSource.Size);
        HashCombine(result, std::hash<uint64_t>{}(key.FragmentSource.Id));
        HashCombine(result,
                    std::hash<uint64_t>{}(key.FragmentSource.SecondaryHash));
        HashCombine(result, key.FragmentSource.Size);
        return result;
    }
};

CanonicalKey BuildCanonicalKey(
    const PicaShaderPipelineRequest& request,
    const ::Oot3d::Renderer::PicaShaderSourceIdentity& vertexSource,
    const ::Oot3d::Renderer::PicaShaderSourceIdentity& fragmentSource) noexcept {
    return {
        request.VertexShaderKey,
        request.FragmentShaderKey,
        vertexSource,
        fragmentSource,
    };
}

struct CanonicalEntry {
    uint64_t Identity = 0;
    PicaShaderPipelineResult Result;
};

struct InstrumentationKey {
    uint64_t CanonicalIdentity = 0;
    uint32_t ReflectionReflectivity = 0;
    uint32_t ReflectionRoughness = 0;
    uint32_t ReflectionMaterialClass = 0;
    uint32_t RequestedFeatures = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint16_t StateBits = 0;
    uint32_t BlendStateBits = 0;
    uint8_t FragmentOperationMode = 0;
    uint8_t ColorWriteMask = 0;
    uint8_t CompositionDomain = 0;
    uint8_t DepthCompare = 0;

    bool operator==(const InstrumentationKey&) const = default;
};

struct InstrumentationKeyHash {
    size_t operator()(const InstrumentationKey& key) const noexcept {
        size_t result = std::hash<uint64_t>{}(key.CanonicalIdentity);
        HashCombine(result, key.ReflectionReflectivity);
        HashCombine(result, key.ReflectionRoughness);
        HashCombine(result, key.ReflectionMaterialClass);
        HashCombine(result, key.RequestedFeatures);
        HashCombine(result, key.FramebufferWidth);
        HashCombine(result, key.FramebufferHeight);
        HashCombine(result, key.StateBits);
        HashCombine(result, key.BlendStateBits);
        HashCombine(result, key.FragmentOperationMode);
        HashCombine(result, key.ColorWriteMask);
        HashCombine(result, key.CompositionDomain);
        HashCombine(result, key.DepthCompare);
        return result;
    }
};

PicaShaderInstrumentationFeature BuildRequestedFeatures(
    const PicaShaderPipelineRequest& request,
    const EffectsSettings& effects) noexcept {
    PicaShaderInstrumentationFeature features =
        PicaShaderInstrumentationFeature::None;
    const bool sceneDraw =
        request.Draw.CompositionDomain ==
        ::Oot3d::Renderer::PicaCompositionDomain::Scene;
    if (sceneDraw && request.Draw.FragmentOperationMode != 3U &&
        effects.Toon != ToonMode::Off) {
        features |= PicaShaderInstrumentationFeature::Toon;
    }
    if (sceneDraw && request.TemporalMotionEnabled) {
        features |= PicaShaderInstrumentationFeature::TemporalVertex;
        features |= PicaShaderInstrumentationFeature::ReactiveMask;
        features |= PicaShaderInstrumentationFeature::RigidMotionGuide;
    }
    if (sceneDraw && request.Draw.DirectionalShadowReceiver &&
        request.Draw.FragmentOperationMode == 0U &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 0x7U) != 0U &&
        request.Draw.PerspectiveProjection) {
        features |=
            PicaShaderInstrumentationFeature::DirectionalShadowLighting;
    }
    if (sceneDraw &&
        effects.AmbientOcclusion == AmbientOcclusionMode::Cacao &&
        request.Draw.FragmentOperationMode == 0U &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 0x7U) != 0U) {
        features |=
            PicaShaderInstrumentationFeature::AmbientOcclusionGuide;
    }
    if (sceneDraw &&
        (effects.AmbientOcclusion == AmbientOcclusionMode::Cacao ||
         (effects.Toon != ToonMode::Off &&
          effects.ToonStyle.OutlineEnabled))) {
        features |= PicaShaderInstrumentationFeature::SceneDomainGuide;
    }
    if (sceneDraw &&
        (effects.AmbientOcclusion == AmbientOcclusionMode::Cacao ||
         (effects.Toon != ToonMode::Off &&
          effects.ToonStyle.OutlineEnabled) ||
         effects.Reflections != ReflectionMode::Off)) {
        features |= PicaShaderInstrumentationFeature::NormalGuide;
    }
    if (sceneDraw && request.Draw.DepthWriteEnabled && effects.Toon != ToonMode::Off &&
        effects.ToonStyle.OutlineEnabled) {
        features |=
            PicaShaderInstrumentationFeature::NativeFogGuide | PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    }
    if (sceneDraw && effects.Reflections != ReflectionMode::Off) {
        features |=
            PicaShaderInstrumentationFeature::ReflectionMaterialGuide;
    }
    return features;
}

InstrumentationKey BuildInstrumentationKey(
    uint64_t canonicalIdentity,
    const PicaShaderPipelineRequest& request,
    PicaShaderInstrumentationFeature requestedFeatures) noexcept {
    InstrumentationKey key;
    key.CanonicalIdentity = canonicalIdentity;
    key.RequestedFeatures = static_cast<uint32_t>(requestedFeatures);
    key.FramebufferWidth = request.Draw.FramebufferWidth;
    key.FramebufferHeight = request.Draw.FramebufferHeight;
    key.FragmentOperationMode = request.Draw.FragmentOperationMode;
    key.ColorWriteMask = request.Draw.ColorWriteMask;
    key.CompositionDomain = static_cast<uint8_t>(
        request.Draw.CompositionDomain);
    key.DepthCompare = static_cast<uint8_t>(request.Draw.DepthCompare);
    const auto& blend = request.Draw.Blend;
    key.BlendStateBits =
        static_cast<uint32_t>(blend.EquationRgb) |
        (static_cast<uint32_t>(blend.EquationAlpha) << 3U) |
        (static_cast<uint32_t>(blend.SourceRgb) << 6U) |
        (static_cast<uint32_t>(blend.DestRgb) << 10U) |
        (static_cast<uint32_t>(blend.SourceAlpha) << 14U) |
        (static_cast<uint32_t>(blend.DestAlpha) << 18U);
    key.StateBits =
        (request.Draw.DepthTestEnabled ? 1U : 0U) |
        (request.Draw.DepthWriteEnabled ? 1U << 1U : 0U) |
        (request.Draw.Blend.Enabled ? 1U << 2U : 0U) |
        (request.Draw.PerspectiveProjection ? 1U << 3U : 0U) |
        (request.TemporalMotionEnabled ? 1U << 4U : 0U) |
        (request.ReflectionProfile.has_value() ? 1U << 5U : 0U);
    if (request.ReflectionProfile.has_value()) {
        key.ReflectionReflectivity =
            std::bit_cast<uint32_t>(
                request.ReflectionProfile->Reflectivity);
        key.ReflectionRoughness =
            std::bit_cast<uint32_t>(request.ReflectionProfile->Roughness);
        key.ReflectionMaterialClass =
            std::bit_cast<uint32_t>(
                request.ReflectionProfile->MaterialClass);
    }
    return key;
}

PicaShaderPipelineResult BuildInstrumentedVariant(
    const PicaShaderPipelineResult& canonical,
    const PicaShaderPipelineRequest& request,
    const EffectsSettings& effects,
    PicaShaderInstrumentationFeature requestedFeatures) {
    PicaShaderPipelineResult result = canonical;
    result.RequestedFeatures = requestedFeatures;

    const auto apply = [&result](
                           PicaShaderInstrumentationFeature feature) {
        result.AppliedFeatures |= feature;
        result.Domain = PicaShaderDomain::Instrumented;
    };

    if (HasPicaShaderInstrumentationFeature(
            requestedFeatures,
            PicaShaderInstrumentationFeature::TemporalVertex)) {
        auto temporal = BuildPicaTemporalVertexInstrumentation(
            result.VertexShaderSource, result.VertexShaderKey,
            request.TemporalVertexProgram);
        result.TemporalVertexApplied = temporal.Applied;
        result.DirectTemporalVertexProgramUsed =
            temporal.UsedProvidedProgram;
        if (temporal.Applied) {
            result.VertexShaderSource = std::move(temporal.Source);
            result.VertexShaderKey = temporal.FragmentKey;
            apply(PicaShaderInstrumentationFeature::TemporalVertex);
        }
    }

    const auto fragment = BuildPicaFragmentInstrumentationVariant({
        result.FragmentShaderSource,
        result.FragmentShaderKey,
        requestedFeatures,
        {
            request.Draw.FramebufferWidth,
            request.Draw.FramebufferHeight,
            request.Draw.FragmentOperationMode,
            request.Draw.DepthTestEnabled,
            request.Draw.DepthWriteEnabled,
            request.Draw.Blend,
            request.Draw.ColorWriteMask,
            request.Draw.CompositionDomain,
            request.Draw.DepthCompare,
        },
        {
            effects.AmbientOcclusion == AmbientOcclusionMode::Cacao,
            effects.Toon != ToonMode::Off &&
                effects.ToonStyle.OutlineEnabled,
        },
        request.ReflectionProfile.has_value()
            ? &*request.ReflectionProfile
            : nullptr,
        &canonical.CanonicalFragmentHooks,
        effects.Toon,
        &effects.ToonStyle,
    });
    result.ToonEligibility = fragment.ToonEligibility;
    result.ToonMaterialPath = fragment.ToonMaterialPath;
    result.DirectionalShadowEligibility =
        fragment.DirectionalShadowEligibility;
    result.AmbientGuideEligibility = fragment.AmbientGuideEligibility;
    result.SceneDomainEligibility = fragment.SceneDomainEligibility;
    result.ReflectionEligibility = fragment.ReflectionEligibility;
    result.ReactiveCoverage = fragment.ReactiveCoverage;
    result.Reactive = fragment.Reactive;
    result.RigidMotionApplied = fragment.RigidMotionApplied;
    result.FragmentOutputs = fragment.Outputs;
    result.DirectFragmentHooksUsed =
        canonical.DirectCanonicalFragmentHooksUsed &&
        fragment.UsedProvidedHooks;
    if (fragment.Applied()) {
        result.FragmentShaderSource = fragment.Source;
        result.FragmentShaderKey = fragment.FragmentKey;
        result.AppliedFeatures |= fragment.AppliedFeatures;
        result.Domain = PicaShaderDomain::Instrumented;
    }
    result.VertexShaderSourceIdentity =
        ::Oot3d::Renderer::IdentifyPicaShaderSource(
            result.VertexShaderSource);
    result.FragmentShaderSourceIdentity =
        ::Oot3d::Renderer::IdentifyPicaShaderSource(
            result.FragmentShaderSource);
    return result;
}

} // namespace

PicaShaderInstrumentationFeature ResolvePicaShaderProfileFeatures(
    const EffectsSettings& effects, bool temporalMotionEnabled,
    bool directionalShadowLightingEnabled) noexcept {
    PicaShaderInstrumentationFeature features =
        PicaShaderInstrumentationFeature::None;
    if (effects.Toon != ToonMode::Off) {
        features |= PicaShaderInstrumentationFeature::Toon;
    }
    if (temporalMotionEnabled) {
        features |= PicaShaderInstrumentationFeature::TemporalVertex;
        features |= PicaShaderInstrumentationFeature::ReactiveMask;
        features |= PicaShaderInstrumentationFeature::RigidMotionGuide;
    }
    if (directionalShadowLightingEnabled) {
        features |=
            PicaShaderInstrumentationFeature::DirectionalShadowLighting;
    }
    if (effects.AmbientOcclusion == AmbientOcclusionMode::Cacao) {
        features |=
            PicaShaderInstrumentationFeature::AmbientOcclusionGuide;
    }
    if (effects.AmbientOcclusion == AmbientOcclusionMode::Cacao ||
        (effects.Toon != ToonMode::Off &&
         effects.ToonStyle.OutlineEnabled)) {
        features |= PicaShaderInstrumentationFeature::SceneDomainGuide;
    }
    if (effects.AmbientOcclusion == AmbientOcclusionMode::Cacao ||
        (effects.Toon != ToonMode::Off &&
         effects.ToonStyle.OutlineEnabled) ||
        effects.Reflections != ReflectionMode::Off) {
        features |= PicaShaderInstrumentationFeature::NormalGuide;
    }
    if (effects.Toon != ToonMode::Off && effects.ToonStyle.OutlineEnabled) {
        features |=
            PicaShaderInstrumentationFeature::NativeFogGuide | PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    }
    if (effects.Reflections != ReflectionMode::Off) {
        features |=
            PicaShaderInstrumentationFeature::ReflectionMaterialGuide;
    }
    return features;
}

class PicaShaderPipelineCache::Impl final {
  public:
    using CanonicalBucket =
        std::vector<std::unique_ptr<CanonicalEntry>>;

    uint64_t SettingsRevision = 0;
    uint64_t CanonicalHits = 0;
    uint64_t CanonicalMisses = 0;
    uint64_t InstrumentationHits = 0;
    uint64_t InstrumentationMisses = 0;
    uint64_t InstrumentationInvalidations = 0;
    uint64_t CanonicalResolves = 0;
    uint64_t InstrumentedResolves = 0;
    uint64_t NativeFidelityConflicts = 0;
    uint64_t CanonicalOutputAudits = 0;
    uint64_t CanonicalOutputContractRejects = 0;
    uint64_t CanonicalCompatibilityAnalyses = 0;
    uint64_t TypedInstrumentationContractRejects = 0;
    uint64_t CanonicalSourceIdentityAudits = 0;
    uint64_t CanonicalSourceIdentityFastHits = 0;
    uint64_t CanonicalSourceIdentityRejects = 0;
    uint64_t NextCanonicalIdentity = 1;
    size_t CanonicalEntryCount = 0;
    bool RevisionInitialized = false;
    std::unordered_map<CanonicalKey, CanonicalBucket, CanonicalKeyHash>
        CanonicalEntries;
    std::unordered_map<InstrumentationKey, PicaShaderPipelineResult,
                       InstrumentationKeyHash>
        InstrumentationEntries;

    std::pair<const CanonicalEntry*, bool> ResolveCanonical(
        const PicaShaderPipelineRequest& request) {
        const bool suppliedVertexIdentity =
            request.VertexShaderSourceIdentity.Available();
        const bool suppliedFragmentIdentity =
            request.FragmentShaderSourceIdentity.Available();
        if ((suppliedVertexIdentity &&
             request.VertexShaderSourceIdentity.Size !=
                 request.VertexShaderSource.size()) ||
            (suppliedFragmentIdentity &&
             request.FragmentShaderSourceIdentity.Size !=
                 request.FragmentShaderSource.size())) {
            ++CanonicalSourceIdentityRejects;
            throw std::logic_error(
                "canonical PICA shader source identity size mismatch");
        }
        const auto vertexSource = suppliedVertexIdentity
            ? request.VertexShaderSourceIdentity
            : ::Oot3d::Renderer::IdentifyPicaShaderSource(
                  request.VertexShaderSource);
        const auto fragmentSource = suppliedFragmentIdentity
            ? request.FragmentShaderSourceIdentity
            : ::Oot3d::Renderer::IdentifyPicaShaderSource(
                  request.FragmentShaderSource);
        const CanonicalKey key =
            BuildCanonicalKey(request, vertexSource, fragmentSource);
        auto& bucket = CanonicalEntries[key];
        for (const auto& entry : bucket) {
            if (entry->Result.VertexShaderKey == request.VertexShaderKey &&
                entry->Result.FragmentShaderKey ==
                    request.FragmentShaderKey &&
                entry->Result.VertexShaderSourceIdentity == vertexSource &&
                entry->Result.FragmentShaderSourceIdentity == fragmentSource) {
                ++CanonicalHits;
                CanonicalSourceIdentityFastHits +=
                    suppliedVertexIdentity + suppliedFragmentIdentity;
                return {entry.get(), true};
            }
        }

        if (suppliedVertexIdentity) {
            ++CanonicalSourceIdentityAudits;
            if (::Oot3d::Renderer::IdentifyPicaShaderSource(
                    request.VertexShaderSource) != vertexSource) {
                ++CanonicalSourceIdentityRejects;
                throw std::logic_error(
                    "canonical PICA vertex source identity mismatch");
            }
        }
        if (suppliedFragmentIdentity) {
            ++CanonicalSourceIdentityAudits;
            if (::Oot3d::Renderer::IdentifyPicaShaderSource(
                    request.FragmentShaderSource) != fragmentSource) {
                ++CanonicalSourceIdentityRejects;
                throw std::logic_error(
                    "canonical PICA fragment source identity mismatch");
            }
        }

        ++CanonicalMisses;
        ++CanonicalOutputAudits;
        const auto auditedOutputs =
            AnalyzePicaFragmentOutputContract(
                request.FragmentShaderSource);
        const bool directHooks =
            request.FragmentShaderHooks.ValidFor(
                request.FragmentShaderSource);
        PicaShaderHookLayout canonicalHooks =
            directHooks
                ? request.FragmentShaderHooks
                : AnalyzePicaFragmentShaderHooks(
                      request.FragmentShaderSource);
        if (!directHooks) {
            ++CanonicalCompatibilityAnalyses;
        }
        if (!auditedOutputs.CanonicalNative() ||
            !canonicalHooks.ValidFor(request.FragmentShaderSource) ||
            !canonicalHooks.Outputs.CanonicalNative() ||
            canonicalHooks.Outputs != auditedOutputs) {
            ++CanonicalOutputContractRejects;
            throw std::logic_error(
                "canonical PICA fragment shader violates native "
                "color/depth output ownership");
        }
        auto entry = std::make_unique<CanonicalEntry>();
        entry->Identity = NextCanonicalIdentity++;
        entry->Result.VertexShaderSource.assign(
            request.VertexShaderSource);
        entry->Result.VertexShaderKey = request.VertexShaderKey;
        entry->Result.VertexShaderSourceIdentity = vertexSource;
        entry->Result.FragmentShaderSource.assign(
            request.FragmentShaderSource);
        entry->Result.FragmentShaderKey = request.FragmentShaderKey;
        entry->Result.FragmentShaderSourceIdentity = fragmentSource;
        entry->Result.CanonicalFragmentHooks = canonicalHooks;
        entry->Result.DirectCanonicalFragmentHooksUsed = directHooks;
        entry->Result.DirectFragmentHooksUsed = directHooks;
        entry->Result.GrassTexture0Sampled =
            canonicalHooks.SamplesTexture(0U);
        const CanonicalEntry* inserted = entry.get();
        bucket.push_back(std::move(entry));
        ++CanonicalEntryCount;
        return {inserted, false};
    }
};

PicaShaderPipelineCache::PicaShaderPipelineCache()
    : mImpl(std::make_unique<Impl>()) {
}

PicaShaderPipelineCache::~PicaShaderPipelineCache() = default;

void PicaShaderPipelineCache::BeginFrame(uint64_t settingsRevision) {
    if (mImpl->RevisionInitialized &&
        mImpl->SettingsRevision == settingsRevision) {
        return;
    }
    if (mImpl->RevisionInitialized) {
        ++mImpl->InstrumentationInvalidations;
    }
    mImpl->InstrumentationEntries.clear();
    mImpl->SettingsRevision = settingsRevision;
    mImpl->RevisionInitialized = true;
}

const PicaShaderPipelineResult& PicaShaderPipelineCache::Resolve(
    const PicaShaderPipelineRequest& request,
    const EffectsSettings& effects,
    bool* cacheHit) {
    const auto [canonical, canonicalHit] =
        mImpl->ResolveCanonical(request);
    const PicaShaderInstrumentationFeature requestedFeatures =
        BuildRequestedFeatures(request, effects);

    if (HasPicaShaderInstrumentationFeature(
            requestedFeatures,
            PicaShaderInstrumentationFeature::TemporalVertex) &&
        !request.TemporalVertexProgram.ValidFor(
            request.VertexShaderSource)) {
        ++mImpl->TypedInstrumentationContractRejects;
        throw std::logic_error(
            "temporal PICA instrumentation requires the typed "
            "frontend vertex program");
    }

    if (request.RequireNativeFidelity) {
        ++mImpl->CanonicalResolves;
        if (requestedFeatures !=
            PicaShaderInstrumentationFeature::None) {
            ++mImpl->NativeFidelityConflicts;
            throw std::logic_error(
                "Native Fidelity does not permit PICA shader "
                "instrumentation");
        }
        if (cacheHit != nullptr) {
            *cacheHit = canonicalHit;
        }
        return canonical->Result;
    }

    if (requestedFeatures ==
        PicaShaderInstrumentationFeature::None) {
        ++mImpl->CanonicalResolves;
        if (cacheHit != nullptr) {
            *cacheHit = canonicalHit;
        }
        return canonical->Result;
    }

    const InstrumentationKey key = BuildInstrumentationKey(
        canonical->Identity, request, requestedFeatures);
    const auto found = mImpl->InstrumentationEntries.find(key);
    if (found != mImpl->InstrumentationEntries.end()) {
        ++mImpl->InstrumentationHits;
        if (found->second.IsCanonical()) {
            ++mImpl->CanonicalResolves;
        } else {
            ++mImpl->InstrumentedResolves;
        }
        if (cacheHit != nullptr) {
            *cacheHit = true;
        }
        return found->second;
    }

    ++mImpl->InstrumentationMisses;
    auto [inserted, unused] = mImpl->InstrumentationEntries.emplace(
        key, BuildInstrumentedVariant(
                 canonical->Result, request, effects,
                 requestedFeatures));
    (void)unused;
    if (inserted->second.IsCanonical()) {
        ++mImpl->CanonicalResolves;
    } else {
        ++mImpl->InstrumentedResolves;
    }
    if (cacheHit != nullptr) {
        *cacheHit = false;
    }
    return inserted->second;
}

PicaShaderPipelineCacheStats PicaShaderPipelineCache::Stats() const noexcept {
    return {
        mImpl->CanonicalHits,
        mImpl->CanonicalMisses,
        mImpl->CanonicalEntryCount,
        mImpl->InstrumentationHits,
        mImpl->InstrumentationMisses,
        mImpl->InstrumentationInvalidations,
        mImpl->InstrumentationEntries.size(),
        mImpl->CanonicalResolves,
        mImpl->InstrumentedResolves,
        mImpl->NativeFidelityConflicts,
        mImpl->CanonicalOutputAudits,
        mImpl->CanonicalOutputContractRejects,
        mImpl->CanonicalCompatibilityAnalyses,
        mImpl->TypedInstrumentationContractRejects,
        mImpl->SettingsRevision,
        mImpl->CanonicalSourceIdentityAudits,
        mImpl->CanonicalSourceIdentityFastHits,
        mImpl->CanonicalSourceIdentityRejects,
    };
}

void PicaShaderPipelineCache::Clear() {
    if (mImpl->RevisionInitialized ||
        !mImpl->InstrumentationEntries.empty()) {
        ++mImpl->InstrumentationInvalidations;
    }
    mImpl->CanonicalEntries.clear();
    mImpl->InstrumentationEntries.clear();
    mImpl->CanonicalEntryCount = 0;
    mImpl->NextCanonicalIdentity = 1;
    mImpl->CanonicalSourceIdentityAudits = 0;
    mImpl->CanonicalSourceIdentityFastHits = 0;
    mImpl->CanonicalSourceIdentityRejects = 0;
    mImpl->SettingsRevision = 0;
    mImpl->RevisionInitialized = false;
}

} // namespace Fast::Oot3d
