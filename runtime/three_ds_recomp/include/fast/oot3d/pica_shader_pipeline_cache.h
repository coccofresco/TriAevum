#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/pica_ambient_occlusion_guide.h"
#include "fast/oot3d/pica_reflection_material.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_scene_domain_guide.h"
#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/oot3d/pica_toon_shader.h"
#include "oot3d/renderer/pica_shader_source_identity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

enum class PicaShaderDomain : uint8_t {
    Canonical,
    Instrumented,
};

struct PicaShaderPipelineDrawInfo {
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t FragmentOperationMode = 0;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    ::Oot3d::Renderer::NativeBlendState Blend;
    uint8_t ColorWriteMask = 0;
    bool PerspectiveProjection = false;
    bool DirectionalShadowReceiver = false;
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    ::Oot3d::Renderer::PicaCompareFunction DepthCompare = ::Oot3d::Renderer::PicaCompareFunction::Less;
};

struct PicaShaderPipelineRequest {
    std::string_view VertexShaderSource;
    uint64_t VertexShaderKey = 0;
    std::string_view FragmentShaderSource;
    uint64_t FragmentShaderKey = 0;
    PicaShaderPipelineDrawInfo Draw;
    bool TemporalMotionEnabled = false;
    std::optional<ReflectionMaterialParameters> ReflectionProfile;
    bool RequireNativeFidelity = false;
    ::Oot3d::Renderer::PicaTemporalVertexProgramView
        TemporalVertexProgram;
    PicaShaderHookLayout FragmentShaderHooks;
    ::Oot3d::Renderer::PicaShaderSourceIdentity VertexShaderSourceIdentity;
    ::Oot3d::Renderer::PicaShaderSourceIdentity FragmentShaderSourceIdentity;
};

struct PicaShaderPipelineResult {
    std::string VertexShaderSource;
    uint64_t VertexShaderKey = 0;
    ::Oot3d::Renderer::PicaShaderSourceIdentity VertexShaderSourceIdentity;
    std::string FragmentShaderSource;
    uint64_t FragmentShaderKey = 0;
    ::Oot3d::Renderer::PicaShaderSourceIdentity FragmentShaderSourceIdentity;
    PicaShaderDomain Domain = PicaShaderDomain::Canonical;
    PicaShaderInstrumentationFeature RequestedFeatures =
        PicaShaderInstrumentationFeature::None;
    PicaShaderInstrumentationFeature AppliedFeatures =
        PicaShaderInstrumentationFeature::None;
    PicaToonEligibility ToonEligibility = PicaToonEligibility::Disabled;
    bool ToonMaterialPath = false;
    bool TemporalVertexApplied = false;
    bool DirectTemporalVertexProgramUsed = false;
    PicaShaderHookLayout CanonicalFragmentHooks;
    bool DirectCanonicalFragmentHooksUsed = false;
    PicaDirectionalShadowLightingEligibility DirectionalShadowEligibility =
        PicaDirectionalShadowLightingEligibility::Disabled;
    PicaAmbientOcclusionGuideEligibility AmbientGuideEligibility =
        PicaAmbientOcclusionGuideEligibility::Disabled;
    PicaSceneDomainGuideEligibility SceneDomainEligibility =
        PicaSceneDomainGuideEligibility::Disabled;
    PicaReflectionMaterialEligibility ReflectionEligibility =
        PicaReflectionMaterialEligibility::Disabled;
    PicaReactiveCoverage ReactiveCoverage =
        PicaReactiveCoverage::None;
    bool Reactive = false;
    bool RigidMotionApplied = false;
    bool DirectFragmentHooksUsed = false;
    bool GrassTexture0Sampled = false;
    PicaFragmentInstrumentationOutputLayout FragmentOutputs;

    [[nodiscard]] bool IsCanonical() const noexcept {
        return Domain == PicaShaderDomain::Canonical;
    }
};

struct PicaShaderPipelineCacheStats {
    uint64_t CanonicalHits = 0;
    uint64_t CanonicalMisses = 0;
    size_t CanonicalEntries = 0;
    uint64_t InstrumentationHits = 0;
    uint64_t InstrumentationMisses = 0;
    uint64_t InstrumentationInvalidations = 0;
    size_t InstrumentationEntries = 0;
    uint64_t CanonicalResolves = 0;
    uint64_t InstrumentedResolves = 0;
    uint64_t NativeFidelityConflicts = 0;
    uint64_t CanonicalOutputAudits = 0;
    uint64_t CanonicalOutputContractRejects = 0;
    uint64_t CanonicalCompatibilityAnalyses = 0;
    uint64_t TypedInstrumentationContractRejects = 0;
    uint64_t SettingsRevision = 0;
    uint64_t CanonicalSourceIdentityAudits = 0;
    uint64_t CanonicalSourceIdentityFastHits = 0;
    uint64_t CanonicalSourceIdentityRejects = 0;
};

[[nodiscard]] PicaShaderInstrumentationFeature
ResolvePicaShaderProfileFeatures(
    const EffectsSettings& effects, bool temporalMotionEnabled,
    bool directionalShadowLightingEnabled) noexcept;

// Owns two independent shader domains. Canonical entries are immutable copies
// of the PICA frontend output and survive graphics-setting changes. Optional
// instrumentation is built from those entries and invalidated only at a
// settings revision boundary.
class PicaShaderPipelineCache final {
  public:
    PicaShaderPipelineCache();
    ~PicaShaderPipelineCache();

    PicaShaderPipelineCache(const PicaShaderPipelineCache&) = delete;
    PicaShaderPipelineCache& operator=(const PicaShaderPipelineCache&) = delete;

    void BeginFrame(uint64_t settingsRevision);
    [[nodiscard]] const PicaShaderPipelineResult& Resolve(
        const PicaShaderPipelineRequest& request,
        const EffectsSettings& effects,
        bool* cacheHit = nullptr);
    [[nodiscard]] PicaShaderPipelineCacheStats Stats() const noexcept;
    void Clear();

  private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
