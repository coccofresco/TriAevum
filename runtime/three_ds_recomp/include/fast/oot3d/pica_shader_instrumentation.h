#pragma once

#include "fast/oot3d/pica_ambient_occlusion_guide.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "fast/oot3d/pica_reflection_material.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_scene_domain_guide.h"
#include "fast/oot3d/pica_toon_shader.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

enum class PicaShaderInstrumentationFeature : uint32_t {
    None = 0,
    Toon = 1U << 0U,
    TemporalVertex = 1U << 1U,
    DirectionalShadowLighting = 1U << 2U,
    AmbientOcclusionGuide = 1U << 3U,
    SceneDomainGuide = 1U << 4U,
    ReflectionMaterialGuide = 1U << 5U,
    ReactiveMask = 1U << 6U,
    RigidMotionGuide = 1U << 7U,
    NormalGuide = 1U << 8U,
    NativeFogGuide = 1U << 9U,
    OutlineGeometryGuide = 1U << 10U,
};

constexpr PicaShaderInstrumentationFeature operator|(PicaShaderInstrumentationFeature left,
                                                     PicaShaderInstrumentationFeature right) noexcept {
    return static_cast<PicaShaderInstrumentationFeature>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr PicaShaderInstrumentationFeature& operator|=(PicaShaderInstrumentationFeature& left,
                                                       PicaShaderInstrumentationFeature right) noexcept {
    left = left | right;
    return left;
}

constexpr bool HasPicaShaderInstrumentationFeature(PicaShaderInstrumentationFeature mask,
                                                   PicaShaderInstrumentationFeature feature) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(feature)) != 0U;
}

using PicaShaderHook = ::Oot3d::Renderer::PicaShaderHook;
using PicaShaderHookLayout = ::Oot3d::Renderer::PicaShaderHookLayout;
using PicaShaderSemantic = ::Oot3d::Renderer::PicaShaderSemantic;
using PicaTextureCoordinateOperation =
    ::Oot3d::Renderer::PicaTextureCoordinateOperation;

struct PicaFragmentInstrumentationDrawInfo {
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t FragmentOperationMode = 0;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    ::Oot3d::Renderer::NativeBlendState Blend;
    uint8_t ColorWriteMask = 0;
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    ::Oot3d::Renderer::PicaCompareFunction DepthCompare = ::Oot3d::Renderer::PicaCompareFunction::Less;
};

struct PicaFragmentInstrumentationRequest {
    std::string_view Source;
    uint64_t FragmentKey = 0;
    PicaShaderInstrumentationFeature RequestedFeatures = PicaShaderInstrumentationFeature::None;
    PicaFragmentInstrumentationDrawInfo Draw;
    PicaSceneDomainGuideFeatures SceneDomainFeatures;
    const ReflectionMaterialParameters* ReflectionProfile = nullptr;
    const PicaShaderHookLayout* Hooks = nullptr;
    ToonMode Toon = ToonMode::Off;
    const ToonStyleSettings* ToonStyle = nullptr;
};

struct PicaFragmentInstrumentationOutputLayout {
    bool SceneDomainBlendedOverlay = false;
    bool SceneDomainNormalOverlay = false;
    bool SceneDomainAmbientOverlay = false;
    bool SceneDomainTransparentDepthOverlay = false;
    bool WritesAmbientGuide = false;
    bool WritesFogGuide = false;
    bool WritesOutlineGeometryGuide = false;

    auto operator<=>(
        const PicaFragmentInstrumentationOutputLayout&) const = default;
};

struct PicaFragmentInstrumentationResult {
    std::string Source;
    uint64_t FragmentKey = 0;
    PicaShaderInstrumentationFeature AppliedFeatures = PicaShaderInstrumentationFeature::None;
    PicaShaderHookLayout Hooks;
    PicaDirectionalShadowLightingEligibility DirectionalShadowEligibility =
        PicaDirectionalShadowLightingEligibility::Disabled;
    PicaAmbientOcclusionGuideEligibility AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::Disabled;
    PicaSceneDomainGuideEligibility SceneDomainEligibility = PicaSceneDomainGuideEligibility::Disabled;
    PicaReflectionMaterialEligibility ReflectionEligibility = PicaReflectionMaterialEligibility::Disabled;
    PicaToonEligibility ToonEligibility =
        PicaToonEligibility::Disabled;
    bool ToonMaterialPath = false;
    PicaReactiveCoverage ReactiveCoverage =
        PicaReactiveCoverage::None;
    bool Reactive = false;
    bool RigidMotionApplied = false;
    bool UsedProvidedHooks = false;
    PicaFragmentInstrumentationOutputLayout Outputs;

    [[nodiscard]] bool Applied() const noexcept {
        return AppliedFeatures != PicaShaderInstrumentationFeature::None;
    }
};

[[nodiscard]] PicaShaderHookLayout AnalyzePicaFragmentShaderHooks(std::string_view source) noexcept;

// Compatibility/audit path only. The native frontend publishes this contract
// directly; the cache uses this parser once per canonical source to reject
// accidental pre-cache MRT contamination.
[[nodiscard]] ::Oot3d::Renderer::PicaFragmentOutputContract
AnalyzePicaFragmentOutputContract(std::string_view source) noexcept;

// Resolves every auxiliary fragment output against one typed hook layout and
// composes one variant. Native color and depth statements are copied without
// replacement; guide outputs are added only at typed insertion points.
[[nodiscard]] PicaFragmentInstrumentationResult
BuildPicaFragmentInstrumentationVariant(const PicaFragmentInstrumentationRequest& request);

} // namespace Fast::Oot3d
