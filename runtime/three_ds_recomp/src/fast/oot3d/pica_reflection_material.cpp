#include "fast/oot3d/pica_reflection_material.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Fast::Oot3d {
namespace {

constexpr std::string_view kLightingEndMarker =
    "    // OOT3D_PICA_MATERIAL_TOON_POINT";
constexpr std::string_view kGuideStart =
    "    float oot3d_specular_signal = clamp(max(max("
    "secondary_fragment_color.r, secondary_fragment_color.g), "
    "secondary_fragment_color.b) * 4.0, 0.0, 1.0);\n";
constexpr std::string_view kGuideAssignment =
    "    pica_material_guide = vec4(oot3d_specular_signal, "
    "clamp(1.0 - oot3d_specular_signal * 0.75, 0.08, 1.0), "
    "1.0, 0.0);\n";
constexpr std::string_view kCalibratedGuide =
    "    float oot3d_specular_peak = max(max("
    "secondary_fragment_color.r, secondary_fragment_color.g), "
    "secondary_fragment_color.b);\n"
    "    float oot3d_specular_energy = dot("
    "secondary_fragment_color.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float oot3d_specular_response = smoothstep("
    "0.015, 0.300, max(oot3d_specular_peak, "
    "oot3d_specular_energy));\n"
    "    float oot3d_material_reflectivity = mix("
    "0.120, 1.0, oot3d_specular_response);\n"
    "    float oot3d_material_roughness = mix("
    "0.720, 0.160, oot3d_specular_response);\n"
    "    pica_material_guide = vec4("
    "oot3d_material_reflectivity, oot3d_material_roughness, "
    "0.75, 0.0); // OOT3D calibrated specular material\n";

bool TevConsumesSpecular(std::string_view source) {
    const size_t lightingEnd = source.find(kLightingEndMarker);
    const size_t guide = source.find(kGuideStart);
    if (lightingEnd == std::string_view::npos ||
        guide == std::string_view::npos ||
        lightingEnd >= guide) {
        return false;
    }
    return source.substr(
        lightingEnd + kLightingEndMarker.size(),
        guide - lightingEnd - kLightingEndMarker.size())
        .find("secondary_fragment_color") != std::string_view::npos;
}

std::string BuildExplicitGuide(
    const ReflectionMaterialParameters& profile) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6)
           << "pica_material_guide = vec4("
           << profile.Reflectivity
           << ", "
           << profile.Roughness
           << ", "
           << profile.MaterialClass
           << ", 0.0); // OOT3D explicit texture material";
    return stream.str();
}

void MixKey(uint64_t& key, uint32_t value) {
    key ^= value;
    key *= 1099511628211ULL;
}

ReflectionMaterialParameters SanitizeProfile(
    const ReflectionMaterialParameters& profile) {
    const auto finiteClamp = [](float value, float minimum,
                                float maximum, float fallback) {
        return std::isfinite(value)
                   ? std::clamp(value, minimum, maximum)
                   : fallback;
    };
    return {
        finiteClamp(profile.Reflectivity, 0.0F, 1.0F, 0.0F),
        finiteClamp(profile.Roughness, 0.02F, 1.0F, 1.0F),
        finiteClamp(profile.MaterialClass, 0.5F, 1.0F, 1.0F),
    };
}

PicaReflectionMaterialEligibility ClassifyDrawState(
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled) {
    if (!reflectionsEnabled)
        return PicaReflectionMaterialEligibility::Disabled;
    if (draw.CompositionDomain !=
        ::Oot3d::Renderer::PicaCompositionDomain::Scene)
        return PicaReflectionMaterialEligibility::OutsideScene;
    if (draw.FragmentOperationMode != 0U)
        return PicaReflectionMaterialEligibility::NonColorOperation;
    if (!draw.DepthTestEnabled || !draw.DepthWriteEnabled)
        return PicaReflectionMaterialEligibility::NoDepth;
    if ((draw.ColorWriteMask & 0x7U) == 0U)
        return PicaReflectionMaterialEligibility::NoRgbOutput;
    return PicaReflectionMaterialEligibility::Eligible;
}

} // namespace

PicaReflectionMaterialEligibility
ClassifyPicaReflectionMaterialDraw(
    std::string_view source,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile) {
    const auto drawEligibility =
        ClassifyDrawState(draw, reflectionsEnabled);
    if (drawEligibility != PicaReflectionMaterialEligibility::Eligible)
        return drawEligibility;
    if (source.find("pica_material_guide") == std::string_view::npos)
        return PicaReflectionMaterialEligibility::UnsupportedShader;
    if (explicitProfile != nullptr)
        return PicaReflectionMaterialEligibility::
            ExplicitTextureProfile;
    if (source.find(kGuideStart) == std::string_view::npos)
        return PicaReflectionMaterialEligibility::NoSpecularTevUse;
    if (source.find(kLightingEndMarker) == std::string_view::npos ||
        source.find(kGuideAssignment) == std::string_view::npos)
        return PicaReflectionMaterialEligibility::UnsupportedShader;
    if (!TevConsumesSpecular(source))
        return PicaReflectionMaterialEligibility::NoSpecularTevUse;
    return PicaReflectionMaterialEligibility::Eligible;
}

PicaReflectionMaterialEligibility
ClassifyPicaReflectionMaterialDraw(
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile) {
    const auto drawEligibility =
        ClassifyDrawState(draw, reflectionsEnabled);
    if (drawEligibility != PicaReflectionMaterialEligibility::Eligible)
        return drawEligibility;
    if (!hooks.Valid())
        return PicaReflectionMaterialEligibility::UnsupportedShader;
    if (explicitProfile != nullptr)
        return PicaReflectionMaterialEligibility::ExplicitTextureProfile;
    if (!hooks.Has(::Oot3d::Renderer::PicaShaderSemantic::
                       SecondaryFragmentColorConsumed)) {
        return PicaReflectionMaterialEligibility::NoSpecularTevUse;
    }
    return PicaReflectionMaterialEligibility::Eligible;
}

PicaReflectionMaterialShaderVariant
BuildPicaReflectionMaterialShaderVariant(
    std::string_view source, uint64_t originalFragmentKey,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile) {
    PicaReflectionMaterialShaderVariant result;
    result.FragmentKey = originalFragmentKey;
    result.Eligibility = ClassifyPicaReflectionMaterialDraw(
        source, draw, reflectionsEnabled, explicitProfile);
    if (!result.Applied()) return result;

    result.Source.assign(source);
    if (explicitProfile != nullptr) {
        const ReflectionMaterialParameters sanitizedProfile =
            SanitizeProfile(*explicitProfile);
        constexpr std::string_view assignment =
            "pica_material_guide =";
        const size_t guide = result.Source.rfind(assignment);
        const size_t semicolon =
            guide == std::string::npos
                ? std::string::npos
                : result.Source.find(';', guide);
        if (guide == std::string::npos ||
            semicolon == std::string::npos) {
            result.Eligibility =
                PicaReflectionMaterialEligibility::
                    UnsupportedShader;
            result.Source.clear();
            return result;
        }
        const std::string replacement =
            BuildExplicitGuide(sanitizedProfile);
        result.Source.replace(
            guide, semicolon + 1U - guide, replacement);
        result.FragmentKey ^= 0x7265666c74787401ULL;
        MixKey(result.FragmentKey,
               std::bit_cast<uint32_t>(
                   sanitizedProfile.Reflectivity));
        MixKey(result.FragmentKey,
               std::bit_cast<uint32_t>(
                   sanitizedProfile.Roughness));
        MixKey(result.FragmentKey,
               std::bit_cast<uint32_t>(
                   sanitizedProfile.MaterialClass));
        return result;
    }

    const std::string original =
        std::string(kGuideStart) + std::string(kGuideAssignment);
    const size_t guide = result.Source.find(original);
    if (guide == std::string::npos) {
        result.Eligibility =
            PicaReflectionMaterialEligibility::UnsupportedShader;
        result.Source.clear();
        return result;
    }
    result.Source.replace(
        guide, original.size(), kCalibratedGuide);
    result.FragmentKey ^= 0x7265666c6d617401ULL;
    return result;
}

} // namespace Fast::Oot3d
