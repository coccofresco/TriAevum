#include "fast/oot3d/pica_scene_domain_guide.h"

namespace Fast::Oot3d {
namespace {

constexpr std::string_view kMain = "void main()";
constexpr std::string_view kAmbientOutput =
    "layout(location=4) out vec4 pica_ambient_guide;\n";

uint64_t VariantKey(
    uint64_t original,
    const PicaSceneDomainGuideFeatures& features,
    bool blended) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        mix(static_cast<uint8_t>(original >> shift));
    }
    constexpr std::string_view salt =
        "OOT3D_PICA_SCENE_DOMAIN_GUIDE_V1";
    for (const char value : salt) {
        mix(static_cast<uint8_t>(value));
    }
    mix(features.AmbientOcclusion ? 1U : 0U);
    mix(features.Outline ? 1U : 0U);
    mix(blended ? 1U : 0U);
    return hash;
}

} // namespace

PicaSceneDomainGuideVariant
BuildPicaSceneDomainGuideVariant(
    std::string_view source, uint64_t fragmentKey,
    const PicaSceneDomainDrawInfo& draw,
    const PicaSceneDomainGuideFeatures& features) {
    PicaSceneDomainGuideVariant result;
    result.FragmentKey = fragmentKey;
    if (!features.AmbientOcclusion && !features.Outline) {
        result.Eligibility =
            PicaSceneDomainGuideEligibility::Disabled;
        return result;
    }
    if (draw.CompositionDomain !=
        ::Oot3d::Renderer::PicaCompositionDomain::Scene) {
        result.Eligibility =
            PicaSceneDomainGuideEligibility::OutsideScene;
        return result;
    }
    if (draw.FragmentOperationMode != 0U ||
        (draw.ColorWriteMask & 0x7U) == 0U) {
        result.Eligibility =
            PicaSceneDomainGuideEligibility::NoRgbOutput;
        return result;
    }
    if (draw.DepthWriteEnabled) {
        result.Eligibility =
            PicaSceneDomainGuideEligibility::SceneGeometry;
        return result;
    }
    const size_t main = source.find(kMain);
    if (main == std::string_view::npos ||
        source.find("pica_color") == std::string_view::npos ||
        (features.Outline &&
         source.find("pica_normal_guide") ==
             std::string_view::npos)) {
        result.Eligibility =
            PicaSceneDomainGuideEligibility::UnsupportedShader;
        return result;
    }

    result.Source.assign(source);
    if (features.AmbientOcclusion &&
        source.find("pica_ambient_guide") ==
            std::string_view::npos) {
        result.Source.insert(main, kAmbientOutput);
    }
    const size_t closingBrace = result.Source.rfind('}');
    if (closingBrace == std::string::npos) {
        result.Source.clear();
        result.Eligibility =
            PicaSceneDomainGuideEligibility::UnsupportedShader;
        return result;
    }

    const char* alpha =
        draw.BlendEnabled ? "pica_color.a" : "0.0";
    std::string assignments;
    if (draw.BlendEnabled) {
        assignments +=
            "    // OOT3D_SCENE_DOMAIN_BLENDED_OVERLAY\n";
    }
    if (features.Outline) {
        assignments +=
            "    pica_normal_guide.a = ";
        assignments += alpha;
        assignments +=
            "; // OOT3D_SCENE_DOMAIN_NORMAL_OVERLAY\n";
    }
    if (features.AmbientOcclusion) {
        assignments +=
            "    pica_ambient_guide = vec4(1.0, 1.0, 1.0, ";
        assignments += alpha;
        assignments +=
            "); // OOT3D_SCENE_DOMAIN_AMBIENT_OVERLAY\n";
    }
    result.Source.insert(closingBrace, assignments);
    result.FragmentKey =
        VariantKey(fragmentKey, features, draw.BlendEnabled);
    result.Eligibility = draw.BlendEnabled
        ? PicaSceneDomainGuideEligibility::AppliedBlendedOverlay
        : PicaSceneDomainGuideEligibility::AppliedOpaqueOverlay;
    return result;
}

} // namespace Fast::Oot3d
