#include "fast/extension/pica_shader_pipeline_cache.h"

#include "fast/extension/pica_rigid_motion.h"

#include <cstdlib>
#include <bit>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fast::Extension {

bool PicaFaceNormalGuideDiagnosticRequested() noexcept {
    static const bool requested = [] {
        const char* value = std::getenv("OOT3D_GRAPHICS_FACE_NORMAL_GUIDE");
        return value != nullptr && value[0] == '1';
    }();
    return requested;
}

namespace {
bool FaceNormalGuideDiagnosticRequested() noexcept { return PicaFaceNormalGuideDiagnosticRequested(); }

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
    uint32_t RequestedFeatures = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint16_t StateBits = 0;
    uint32_t BlendStateBits = 0;
    uint8_t FragmentOperationMode = 0;
    uint8_t ColorWriteMask = 0;
    uint8_t CompositionDomain = 0;
    uint8_t DepthCompare = 0;
    uint8_t PbrState = 0;
    uint8_t ReliefTexture = 0xffU;

    bool operator==(const InstrumentationKey&) const = default;
};

struct InstrumentationKeyHash {
    size_t operator()(const InstrumentationKey& key) const noexcept {
        size_t result = std::hash<uint64_t>{}(key.CanonicalIdentity);
        HashCombine(result, key.ReflectionReflectivity);
        HashCombine(result, key.ReflectionRoughness);
        HashCombine(result, key.RequestedFeatures);
        HashCombine(result, key.FramebufferWidth);
        HashCombine(result, key.FramebufferHeight);
        HashCombine(result, key.StateBits);
        HashCombine(result, key.BlendStateBits);
        HashCombine(result, key.FragmentOperationMode);
        HashCombine(result, key.ColorWriteMask);
        HashCombine(result, key.CompositionDomain);
        HashCombine(result, key.DepthCompare);
        HashCombine(result, key.PbrState);
        HashCombine(result, key.ReliefTexture);
        return result;
    }
};

} // namespace

PicaShaderInstrumentationFeature ResolvePicaDrawInstrumentationFeatures(
    const PicaShaderPipelineRequest& request,
    const EffectsSettings& effects) noexcept {
    PicaShaderInstrumentationFeature features =
        PicaShaderInstrumentationFeature::None;
    const bool sceneDraw =
        request.Draw.CompositionDomain ==
        ::Oot3d::Renderer::PicaCompositionDomain::Scene;
    if (sceneDraw && Renderer3ds::PicaCompositionReceivesSurfaceLighting(request.Draw.CompositionLayer) &&
        request.Draw.FragmentOperationMode != 3U &&
        request.Draw.PerspectiveProjection && request.SurfaceLightingAvailable &&
        (UsesFullSceneLighting(effects.SurfaceLighting) ||
         (effects.SurfaceLighting == SurfaceLightingMode::NativePbr && request.Pbr.TextureSlot < 3U)) &&
        request.TemporalVertexProgram.Hooks.SurfaceProgram() != nullptr &&
        request.FragmentShaderHooks.Supports(PicaShaderHook::PicaLighting) &&
        request.FragmentShaderHooks.Supports(PicaShaderHook::AfterNativeTev) &&
        request.FragmentShaderHooks.Has(PicaShaderSemantic::ViewVector) &&
        request.FragmentShaderHooks.Has(PicaShaderSemantic::PrimaryColorInput)) {
        features |= PicaShaderInstrumentationFeature::NativePerPixelLighting;
        if (request.FragmentLightingOperand && UsesFullSceneLighting(effects.SurfaceLighting))
            features |= PicaShaderInstrumentationFeature::FragmentLightingOperand;
        if (request.FragmentLightingOperand && request.FragmentLightingBalance &&
            UsesFullSceneLighting(effects.SurfaceLighting) &&
            (request.Pbr.TextureSlot >= 3U || (UsesPbrMaterials(effects.SurfaceLighting) &&
                request.Pbr.NativeFragmentLighting && request.Pbr.NativeBalance)) &&
            request.FragmentShaderHooks.NativeLightingCapture.Valid(request.FragmentShaderSource.size(),
                request.FragmentShaderHooks.Offset(PicaShaderHook::PicaLighting)))
            features |= PicaShaderInstrumentationFeature::FragmentLightingBalance;
        if (request.NativeFragmentLitShadows)
            features |= PicaShaderInstrumentationFeature::NativeFragmentLitShadows;
        if (UsesPbrMaterials(effects.SurfaceLighting) && request.Pbr.TextureSlot < 3U)
            features |= PicaShaderInstrumentationFeature::PbrMaterial;
    }
    if (sceneDraw && request.Draw.FragmentOperationMode != 3U &&
        effects.Toon != ToonMode::Off &&
        !HasPicaShaderInstrumentationFeature(features, PicaShaderInstrumentationFeature::PbrMaterial)) {
        features |= PicaShaderInstrumentationFeature::Toon;
    }
    // Every scene draw overwrites motion provenance. Missing typed vertex
    // programs still write invalid/rigid/camera provenance without clip inputs.
    if (sceneDraw && request.TemporalMotionEnabled) {
        if (request.TemporalVertexProgram.ValidFor(request.VertexShaderSource))
            features |= PicaShaderInstrumentationFeature::TemporalVertex;
        features |= PicaShaderInstrumentationFeature::ReactiveMask;
        features |= PicaShaderInstrumentationFeature::RigidMotionGuide;
    }
    if (sceneDraw && request.Draw.DirectionalShadowReceiver &&
        UsesFullSceneLighting(effects.SurfaceLighting) &&
        HasPicaShaderInstrumentationFeature(features, PicaShaderInstrumentationFeature::NativePerPixelLighting) &&
        request.Draw.FragmentOperationMode == 0U &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 0x7U) != 0U &&
        request.Draw.PerspectiveProjection) {
        features |=
            PicaShaderInstrumentationFeature::DirectionalShadowLighting;
    }
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
    if (request.ShadowProbe && !request.RequireNativeFidelity &&
        HasPicaShaderInstrumentationFeature(features, PicaShaderInstrumentationFeature::DirectionalShadowLighting))
        features |= PicaShaderInstrumentationFeature::ShadowProbe;
#endif
    if (sceneDraw && request.Draw.PointShadowReceiver &&
        effects.PointShadows.Mode == PointShadowMode::CubeMaps &&
        UsesFullSceneLighting(effects.SurfaceLighting) &&
        HasPicaShaderInstrumentationFeature(features, PicaShaderInstrumentationFeature::NativePerPixelLighting) &&
        request.Draw.PerspectiveProjection) {
        features |= PicaShaderInstrumentationFeature::PointShadowLighting;
    }
    if (sceneDraw &&
        effects.AmbientOcclusion != AmbientOcclusionMode::Off &&
        request.Draw.FragmentOperationMode == 0U &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 0x7U) != 0U) {
        features |=
            PicaShaderInstrumentationFeature::AmbientOcclusionGuide;
    }
    // GI coverage: overlays attenuate the albedo guide's alpha.
    if (sceneDraw &&
        (effects.AmbientOcclusion != AmbientOcclusionMode::Off ||
         (effects.Toon != ToonMode::Off &&
          effects.ToonStyle.OutlineEnabled) ||
         effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off)) {
        features |= PicaShaderInstrumentationFeature::SceneDomainGuide;
    }
    // GI traces from the normal guide.
    if (sceneDraw &&
        (effects.AmbientOcclusion != AmbientOcclusionMode::Off ||
         (effects.Toon != ToonMode::Off &&
          effects.ToonStyle.OutlineEnabled) ||
         effects.Reflections != ReflectionMode::Off ||
         effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off)) {
        features |= PicaShaderInstrumentationFeature::NormalGuide;
    }
    // AO texture relief belongs to the depth-writing world surfaces the AO
    // providers shade, with one colour texture to derive it from.
    if (HasPicaShaderInstrumentationFeature(features, PicaShaderInstrumentationFeature::NormalGuide) &&
        effects.AmbientOcclusion != AmbientOcclusionMode::Off &&
        effects.AoRelief != AoTextureRelief::Off && request.ReliefTexture < 3U &&
        request.Draw.PerspectiveProjection && request.Draw.FragmentOperationMode == 0U &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 0x7U) != 0U) {
        features |= PicaShaderInstrumentationFeature::NormalGuideTextureRelief;
    }
    if (sceneDraw && request.Draw.DepthWriteEnabled && effects.Toon != ToonMode::Off &&
        effects.ToonStyle.OutlineEnabled) {
        features |=
            PicaShaderInstrumentationFeature::NativeFogGuide | PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    }
    // The face normal of the visible surface, for its readers;
    // OOT3D_GRAPHICS_FACE_NORMAL_GUIDE=1 also publishes it (diagnostics) for
    // draws that already publish guides.
    if (sceneDraw && request.Draw.DepthWriteEnabled && features != PicaShaderInstrumentationFeature::None &&
        (EffectsReadFaceNormal(effects) || GlobalIlluminationReadsFaceNormal(effects.GlobalIllumination) ||
         FaceNormalGuideDiagnosticRequested())) {
        features |= PicaShaderInstrumentationFeature::FaceNormalGuide;
    }
    // GI specular (Brixelizer GI) is a reflection source resolved through the
    // same guide.
    if (sceneDraw && (effects.Reflections != ReflectionMode::Off ||
                      (effects.GlobalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi &&
                       effects.GlobalIllumination.Specular))) {
        features |=
            PicaShaderInstrumentationFeature::ReflectionMaterialGuide;
    }
    if (sceneDraw && effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off) {
        features |= PicaShaderInstrumentationFeature::DiffuseAlbedoGuide;
    }
    return features;
}

namespace {

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
    if (HasPicaShaderInstrumentationFeature(requestedFeatures, PicaShaderInstrumentationFeature::PbrMaterial))
        key.PbrState = request.Pbr.TextureSlot | (request.Pbr.NativeFragmentLighting ? 4U : 0U) |
            (request.Pbr.NativeAttenuationLuts ? 8U : 0U) | (request.Pbr.SeamSoftening ? 16U : 0U) |
            (request.Pbr.NativeBalance ? 32U : 0U) | (request.Pbr.PreserveNativeUnlit ? 64U : 0U);
    if (HasPicaShaderInstrumentationFeature(requestedFeatures,
                                            PicaShaderInstrumentationFeature::NormalGuideTextureRelief))
        key.ReliefTexture = request.ReliefTexture;
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
        (request.ReflectionProfile.has_value() ? 1U << 5U : 0U) |
        // Every other request input BuildInstrumentedVariant forwards to
        // the generators: the isolated-lighting guides and the typed
        // vertex contract that selects the authored-normal paths.
        (request.Draw.IsolatedLighting ? 1U << 6U : 0U) |
        (request.TemporalVertexProgram.ValidFor(request.VertexShaderSource) ? 1U << 7U : 0U) |
        (request.TemporalVertexProgram.Hooks.SurfaceProgram() != nullptr ? 1U << 8U : 0U) |
        (request.TemporalVertexProgram.Hooks.TransformProgram() != nullptr ? 1U << 9U : 0U);
    if (request.ReflectionProfile.has_value()) {
        key.ReflectionReflectivity =
            std::bit_cast<uint32_t>(
                request.ReflectionProfile->Reflectivity);
        key.ReflectionRoughness =
            std::bit_cast<uint32_t>(request.ReflectionProfile->Roughness);
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

    const bool temporalRequested = HasPicaShaderInstrumentationFeature(
        requestedFeatures, PicaShaderInstrumentationFeature::TemporalVertex);
    const bool surfaceRequested = HasPicaShaderInstrumentationFeature(
        requestedFeatures, PicaShaderInstrumentationFeature::NativePerPixelLighting);
    // The normal guide reads the authored vertex normal wherever the typed
    // program exposes it, whatever the lighting mode: never a normal
    // reconstructed from depth derivatives when the model supplies its own.
    const bool guideNormalRequested =
        HasPicaShaderInstrumentationFeature(requestedFeatures, PicaShaderInstrumentationFeature::NormalGuide) &&
        request.TemporalVertexProgram.Hooks.SurfaceProgram() != nullptr &&
        request.TemporalVertexProgram.Hooks.TransformProgram() != nullptr;
    auto fragmentFeatures = requestedFeatures;
    bool surfaceNormalInput = false;
    if (temporalRequested || surfaceRequested || guideNormalRequested) {
        auto vertex = BuildPicaVertexInstrumentation(
            result.VertexShaderSource, result.VertexShaderKey,
            request.TemporalVertexProgram, temporalRequested, surfaceRequested || guideNormalRequested);
        result.TemporalVertexApplied = vertex.TemporalApplied;
        result.DirectTemporalVertexProgramUsed = temporalRequested && vertex.UsedProvidedProgram;
        if (vertex.Applied) {
            result.VertexShaderSource = std::move(vertex.Source);
            result.VertexShaderKey = vertex.FragmentKey;
            if (vertex.TemporalApplied) apply(PicaShaderInstrumentationFeature::TemporalVertex);
        }
        surfaceNormalInput = vertex.SurfaceApplied;
        result.SurfaceVertexApplied = vertex.SurfaceApplied;
        if (!vertex.SurfaceApplied) {
            fragmentFeatures = static_cast<PicaShaderInstrumentationFeature>(
                static_cast<uint32_t>(fragmentFeatures) &
                ~static_cast<uint32_t>(PicaShaderInstrumentationFeature::NativePerPixelLighting |
                                      PicaShaderInstrumentationFeature::PbrMaterial));
        }
    }

    const auto fragment = BuildPicaFragmentInstrumentationVariant({
        result.FragmentShaderSource,
        result.FragmentShaderKey,
        fragmentFeatures,
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
            request.Draw.PerspectiveProjection,
            request.Draw.IsolatedLighting,
        },
        {
            effects.AmbientOcclusion != AmbientOcclusionMode::Off,
            effects.Toon != ToonMode::Off &&
                effects.ToonStyle.OutlineEnabled,
            effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off,
        },
        request.ReflectionProfile.has_value()
            ? &*request.ReflectionProfile
            : nullptr,
        &canonical.CanonicalFragmentHooks,
        effects.Toon,
        &effects.ToonStyle,
        true,
        request.Pbr,
        surfaceNormalInput,
        request.ReliefTexture,
        AoTextureReliefGain(effects.AoRelief),
        result.TemporalVertexApplied,
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
    result.TypedNormalGuide = fragment.Applied() && fragment.TypedNormalGuide;
    result.FragmentOutputs = fragment.Outputs;
    result.Decisions = fragment.Decisions;
    result.ReadsDrawControl = fragment.ReadsDrawControl;
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
    if (effects.SurfaceLighting != SurfaceLightingMode::Native) {
        features |= PicaShaderInstrumentationFeature::NativePerPixelLighting;
    }
    if (UsesPbrMaterials(effects.SurfaceLighting)) {
        features |= PicaShaderInstrumentationFeature::PbrMaterial;
    }
    if (effects.Toon != ToonMode::Off) {
        features |= PicaShaderInstrumentationFeature::Toon;
    }
    if (temporalMotionEnabled) {
        features |= PicaShaderInstrumentationFeature::TemporalVertex;
        features |= PicaShaderInstrumentationFeature::ReactiveMask;
        features |= PicaShaderInstrumentationFeature::RigidMotionGuide;
    }
    if (directionalShadowLightingEnabled && UsesFullSceneLighting(effects.SurfaceLighting)) {
        features |=
            PicaShaderInstrumentationFeature::DirectionalShadowLighting;
    }
    if (effects.AmbientOcclusion != AmbientOcclusionMode::Off) {
        features |=
            PicaShaderInstrumentationFeature::AmbientOcclusionGuide;
    }
    if (effects.AmbientOcclusion != AmbientOcclusionMode::Off ||
        (effects.Toon != ToonMode::Off &&
         effects.ToonStyle.OutlineEnabled) ||
        effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off) {
        features |= PicaShaderInstrumentationFeature::SceneDomainGuide;
    }
    if (effects.AmbientOcclusion != AmbientOcclusionMode::Off ||
        (effects.Toon != ToonMode::Off &&
         effects.ToonStyle.OutlineEnabled) ||
        effects.Reflections != ReflectionMode::Off ||
        effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off) {
        features |= PicaShaderInstrumentationFeature::NormalGuide;
    }
    if (effects.AmbientOcclusion != AmbientOcclusionMode::Off &&
        effects.AoRelief != AoTextureRelief::Off) {
        features |= PicaShaderInstrumentationFeature::NormalGuideTextureRelief;
    }
    if (effects.Toon != ToonMode::Off && effects.ToonStyle.OutlineEnabled) {
        features |=
            PicaShaderInstrumentationFeature::NativeFogGuide | PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    }
    if (features != PicaShaderInstrumentationFeature::None &&
        (EffectsReadFaceNormal(effects) || GlobalIlluminationReadsFaceNormal(effects.GlobalIllumination) ||
         FaceNormalGuideDiagnosticRequested()))
        features |= PicaShaderInstrumentationFeature::FaceNormalGuide;
    if (effects.Reflections != ReflectionMode::Off ||
        (effects.GlobalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi &&
         effects.GlobalIllumination.Specular)) {
        features |=
            PicaShaderInstrumentationFeature::ReflectionMaterialGuide;
    }
    if (effects.GlobalIllumination.Mode != GlobalIlluminationMode::Off) {
        features |= PicaShaderInstrumentationFeature::DiffuseAlbedoGuide;
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
                if (!entry->Result.DirectCanonicalFragmentHooksUsed &&
                    request.FragmentShaderHooks.ValidFor(request.FragmentShaderSource)) {
                    if (request.FragmentShaderHooks.Outputs !=
                        entry->Result.CanonicalFragmentHooks.Outputs) {
                        ++CanonicalOutputContractRejects;
                        throw std::logic_error(
                            "typed PICA fragment hooks contradict audited native outputs");
                    }
                    // A restored replay can populate this source before the live
                    // frontend supplies its authoritative semantic contract.
                    entry->Result.CanonicalFragmentHooks = request.FragmentShaderHooks;
                    entry->Result.DirectCanonicalFragmentHooksUsed = true;
                    entry->Result.DirectFragmentHooksUsed = true;
                    entry->Result.GrassTexture0Sampled =
                        request.FragmentShaderHooks.SamplesTexture(0U);
                    // Re-key derived variants without invalidating references
                    // held by draws already recorded in the current frame.
                    entry->Identity = NextCanonicalIdentity++;
                }
                if (request.FragmentLightingBalance &&
                    request.FragmentShaderHooks.ValidFor(request.FragmentShaderSource) &&
                    request.FragmentShaderHooks.NativeLightingCapture.Valid(request.FragmentShaderSource.size(),
                        request.FragmentShaderHooks.Offset(PicaShaderHook::PicaLighting)) &&
                    !entry->Result.CanonicalFragmentHooks.NativeLightingCapture.Valid(request.FragmentShaderSource.size(),
                        entry->Result.CanonicalFragmentHooks.Offset(PicaShaderHook::PicaLighting))) {
                    // An old pack can have valid legacy hooks but no component
                    // sites. Upgrade this optional contract only for its user.
                    entry->Result.CanonicalFragmentHooks.NativeLightingCapture =
                        request.FragmentShaderHooks.NativeLightingCapture;
                    entry->Identity = NextCanonicalIdentity++;
                }
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
        ResolvePicaDrawInstrumentationFeatures(request, effects);

    if (request.TemporalMotionEnabled &&
        request.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
        !request.TemporalVertexProgram.ValidFor(
            request.VertexShaderSource)) {
        ++mImpl->TypedInstrumentationContractRejects;
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

} // namespace Fast::Extension
