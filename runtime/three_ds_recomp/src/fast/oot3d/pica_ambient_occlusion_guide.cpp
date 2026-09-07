#include "fast/oot3d/pica_ambient_occlusion_guide.h"

namespace Fast::Oot3d {
namespace {

constexpr std::string_view kReadyMarker =
    "// OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY";
constexpr std::string_view kMain = "void main()";
constexpr std::string_view kOutput =
    "layout(location=4) out vec4 pica_ambient_guide;\n";
constexpr std::string_view kExactOutput =
    "\n    pica_ambient_guide = vec4("
    "clamp(oot3d_ao_response_rgb, vec3(0.0), vec3(1.0)), 1.0);";
constexpr std::string_view kFallbackOutput =
    "\n    pica_ambient_guide = vec4(1.0, 1.0, 1.0, 1.0);\n";

uint64_t VariantKey(uint64_t original) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<uint8_t>(original >> shift);
        hash *= 1099511628211ULL;
    }
    constexpr std::string_view salt =
        "OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_V3";
    for (const char value : salt) {
        hash ^= static_cast<uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

PicaAmbientOcclusionGuideVariant
BuildPicaAmbientOcclusionGuideVariant(
    std::string_view source,
    uint64_t originalFragmentKey,
    bool enabled) {
    PicaAmbientOcclusionGuideVariant result;
    result.FragmentKey = originalFragmentKey;
    if (!enabled) {
        result.Eligibility =
            PicaAmbientOcclusionGuideEligibility::Disabled;
        return result;
    }
    const size_t main = source.find(kMain);
    const size_t closingBrace = source.rfind('}');
    if (main == std::string_view::npos ||
        closingBrace == std::string_view::npos ||
        closingBrace < main) {
        result.Eligibility =
            PicaAmbientOcclusionGuideEligibility::
                UnsupportedShader;
        return result;
    }
    result.Source.assign(source);
    result.Source.insert(main, kOutput);
    const bool exact =
        source.find(kReadyMarker) != std::string_view::npos;
    const size_t adjustedClosingBrace =
        closingBrace + kOutput.size();
    if (exact) {
        result.Source.insert(adjustedClosingBrace, kExactOutput);
        result.Eligibility =
            PicaAmbientOcclusionGuideEligibility::AppliedExact;
    } else {
        result.Source.insert(adjustedClosingBrace,
                             kFallbackOutput);
        result.Eligibility =
            PicaAmbientOcclusionGuideEligibility::AppliedFallback;
    }
    result.FragmentKey = VariantKey(originalFragmentKey);
    return result;
}

} // namespace Fast::Oot3d
