#pragma once

#include "fast/oot3d/pica_ambient_occlusion_guide.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "fast/oot3d/pica_effect_decisions.h"
#include "fast/oot3d/pica_reflection_material.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_scene_domain_guide.h"
#include "fast/oot3d/pica_toon_shader.h"
#include "oot3d/renderer/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_pbr_shading.h"

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
    NativePerPixelLighting = 1U << 11U,
    PbrMaterial = 1U << 12U,
    DiffuseAlbedoGuide = 1U << 13U,
    // Applied only (never requested): an opaque guide writer without a
    // material of its own overwrites the material guide with the neutral
    // baseline, so no earlier sample's value survives under it.
    MaterialGuideBaseline = 1U << 14U,
    // AO texture relief: the luminance slope of the material's colour texture
    // bends the normal guide along the surface's texture axes.
    NormalGuideTextureRelief = 1U << 15U,
    // Point lights attenuated by their shadow cube maps (per-pixel lighting).
    PointShadowLighting = 1U << 16U,
    // The face normal and kind of the visible surface in the geometry guide
    // (renderer3ds/pica_geometry_guide.h), from every depth-writing draw.
    FaceNormalGuide = 1U << 17U,
    FragmentLightingOperand = 1U << 18U,
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
    ShadowProbe = 1U << 19U,
#endif
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
    bool PerspectiveProjection = false;
    // PicaShaderPipelineDrawInfo::IsolatedLighting.
    bool IsolatedLighting = false;
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
    bool ToonParametersUniform = false;
    Renderer3ds::PicaPbrShaderOptions Pbr;
    // The vertex stage outputs the typed authored normal (pica_surface_normal,
    // location 9); the normal guide reads it in every lighting mode.
    bool SurfaceNormalInput = false;
    // NormalGuideTextureRelief: the colour texture unit and the relief gain
    // (normal tilt per unit change of log2 luminance across one texel).
    uint8_t ReliefTexture = 0xffU;
    float ReliefGain = 0.0F;
};

struct PicaFragmentInstrumentationOutputLayout {
    bool SceneDomainBlendedOverlay = false;
    bool SceneDomainNormalOverlay = false;
    bool SceneDomainAmbientOverlay = false;
    bool SceneDomainTransparentDepthOverlay = false;
    // The diffuse albedo guide's alpha carries the overlay's transmission.
    bool SceneDomainAlbedoOverlay = false;
    bool WritesAmbientGuide = false;
    bool WritesFogGuide = false;
    // Geometry guide channels: native normal and depth (outlines), and
    // the visible surface's face normal and kind.
    bool WritesOutlineGeometryGuide = false;
    bool WritesFaceNormalGuide = false;
    bool WritesDiffuseAlbedoGuide = false;

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
    // The normal guide reads the authored vertex normal (SurfaceNormalInput).
    bool TypedNormalGuide = false;
    PicaFragmentInstrumentationOutputLayout Outputs;
    // Every composition branch taken, for the always-available program.
    PicaEffectDecisions Decisions;

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

// The native-view normal guide from view derivatives taken at the main
// prologue, before the native alpha test can end an invocation of the quad.
inline constexpr std::string_view kViewDerivativesPrologue =
    "\n    vec3 oot3d_view_dx = dFdx(pica_view), oot3d_view_dy = dFdy(pica_view);\n";
inline constexpr std::string_view kNormalGuideSurfaceFromDerivatives = R"glsl(
vec3 oot3d_normal_guide_surface(vec4 q, vec3 to_eye, vec3 to_eye_dx, vec3 to_eye_dy) {
    // Vertex-lit PICA programs can disable the normal-quaternion output.
    // Their view vector still describes geometry in the same camera basis.
    vec3 geometric = cross(to_eye_dx, to_eye_dy);
    float area = dot(geometric, geometric);
    if (dot(q, q) >= 0.000001) return oot3d_normal_guide_rotate_z(q);
    if (area <= 1.0e-20) return vec3(0.0, 0.0, 1.0);
    geometric *= inversesqrt(area);
    return dot(geometric, to_eye) < 0.0 ? -geometric : geometric;
}
)glsl";
inline constexpr std::string_view kNativeViewNormalFromDerivatives =
    "oot3d_normal_guide_surface(pica_normquat, pica_view, oot3d_view_dx, oot3d_view_dy)";

// AO texture relief: the height/bend functions (gain as GLSL text: the
// specialized literal or a uniform), the taps at the main prologue (texture
// unit and coordinate as GLSL text) and the bend after every guide reader.
[[nodiscard]] std::string PicaTextureReliefDeclarations(std::string_view gain);
[[nodiscard]] std::string PicaTextureReliefPrologue(std::string_view unit, std::string_view coordinate);
inline constexpr std::string_view kPicaTextureReliefBend =
    "    pica_normal_guide.rgb = oot3d_relief_normal(pica_normal_guide.rgb * 2.0 - 1.0,\n"
    "        oot3d_relief_slope, oot3d_relief_dpdx, oot3d_relief_dpdy, oot3d_relief_duvdx,\n"
    "        oot3d_relief_duvdy) * 0.5 + 0.5;\n";

// Resolves every auxiliary fragment output against one typed hook layout and
// composes one variant. Native color and depth statements are copied without
// replacement; guide outputs are added only at typed insertion points.
[[nodiscard]] PicaFragmentInstrumentationResult
BuildPicaFragmentInstrumentationVariant(const PicaFragmentInstrumentationRequest& request);

} // namespace Fast::Oot3d
