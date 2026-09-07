#pragma once

#include "oot3d/renderer/pica_render_backend.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

inline constexpr std::string_view
    kPicaSceneDomainBlendedOverlayMarker =
        "OOT3D_SCENE_DOMAIN_BLENDED_OVERLAY";
inline constexpr std::string_view
    kPicaSceneDomainNormalOverlayMarker =
        "OOT3D_SCENE_DOMAIN_NORMAL_OVERLAY";
inline constexpr std::string_view
    kPicaSceneDomainAmbientOverlayMarker =
        "OOT3D_SCENE_DOMAIN_AMBIENT_OVERLAY";

struct PicaSceneDomainDrawInfo {
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    uint8_t FragmentOperationMode = 0U;
    bool DepthWriteEnabled = false;
    bool BlendEnabled = false;
    uint8_t ColorWriteMask = 0U;
};

struct PicaSceneDomainGuideFeatures {
    bool AmbientOcclusion = false;
    bool Outline = false;
};

enum class PicaSceneDomainGuideEligibility : uint8_t {
    AppliedBlendedOverlay,
    AppliedOpaqueOverlay,
    Disabled,
    SceneGeometry,
    OutsideScene,
    NoRgbOutput,
    UnsupportedShader,
};

struct PicaSceneDomainGuideVariant {
    std::string Source;
    uint64_t FragmentKey = 0U;
    PicaSceneDomainGuideEligibility Eligibility =
        PicaSceneDomainGuideEligibility::Disabled;

    [[nodiscard]] bool Applied() const noexcept {
        return Eligibility ==
                   PicaSceneDomainGuideEligibility::
                       AppliedBlendedOverlay ||
               Eligibility ==
                   PicaSceneDomainGuideEligibility::
                       AppliedOpaqueOverlay;
    }
};

// Scene geometry writes guide coverage one. Scene-domain draws that do not
// write depth attenuate that coverage by their output alpha, keeping
// post-process effects below native overlays without inferring ownership
// from framebuffer dimensions.
[[nodiscard]] PicaSceneDomainGuideVariant
BuildPicaSceneDomainGuideVariant(
    std::string_view source, uint64_t fragmentKey,
    const PicaSceneDomainDrawInfo& draw,
    const PicaSceneDomainGuideFeatures& features);

} // namespace Fast::Oot3d
