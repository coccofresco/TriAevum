#include "fast/oot3d/pica_toon_shader.h"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Fast::Oot3d {
namespace {

constexpr std::string_view kDepthMarker =
    "    float pica_z_over_w = -gl_FragCoord.z;";
constexpr std::string_view kMaterialMarker =
    "    // OOT3D_PICA_MATERIAL_TOON_POINT";

uint64_t AppendHash(uint64_t hash, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t VariantKey(uint64_t original, ToonMode mode,
                    const ToonStyleSettings& style) {
    uint64_t hash = 1469598103934665603ULL;
    hash = AppendHash(hash, static_cast<uint32_t>(original));
    hash = AppendHash(hash, static_cast<uint32_t>(original >> 32));
    hash = AppendHash(hash, static_cast<uint32_t>(mode));
    const size_t bandCount = std::clamp<size_t>(
        style.LightBands, 2U, kMaximumToonLightBands);
    hash = AppendHash(hash, static_cast<uint32_t>(bandCount));
    hash = AppendHash(hash, style.CustomLightBands ? 1U : 0U);
    auto addFloat = [&](float value) {
        hash = AppendHash(hash, std::bit_cast<uint32_t>(value));
    };
    if (style.CustomLightBands) {
        for (size_t index = 0; index < bandCount; ++index) {
            addFloat(style.LightBandLevels[index]);
        }
        for (size_t index = 0; index + 1U < bandCount; ++index) {
            addFloat(style.LightBandThresholds[index]);
        }
    }
    addFloat(style.BandSoftness);
    addFloat(style.Saturation);
    for (float value : style.ShadowTint) addFloat(value);
    addFloat(style.ShadowStrength);
    addFloat(style.RimStrength);
    addFloat(style.RimWidth);
    for (float value : style.RimTint) addFloat(value);
    return hash;
}

std::string Float(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

std::string BandLuminanceHelper(const ToonStyleSettings& style) {
    if (!style.CustomLightBands) {
        return R"(
float oot3d_toon_band_luminance(float guide) {
    float scaled = guide * OOT3D_TOON_BAND_SPAN;
    float lowerBand = floor(scaled);
    float transition = smoothstep(0.5 - OOT3D_TOON_SOFTNESS,
                                  0.5 + OOT3D_TOON_SOFTNESS,
                                  fract(scaled));
    return (lowerBand + transition) / OOT3D_TOON_BAND_SPAN;
}
)";
    }

    const size_t bandCount = std::clamp<size_t>(
        style.LightBands, 2U, kMaximumToonLightBands);
    const size_t thresholdCount = bandCount - 1U;
    std::string source =
        "\nfloat oot3d_toon_band_luminance(float guide) {\n"
        "    float bandLuminance = " +
        Float(style.LightBandLevels[0]) + ";\n";
    for (size_t index = 0; index < thresholdCount; ++index) {
        float spacing = 1.0F;
        if (thresholdCount > 1U) {
            if (index == 0U) {
                spacing = style.LightBandThresholds[1] -
                          style.LightBandThresholds[0];
            } else if (index + 1U == thresholdCount) {
                spacing = style.LightBandThresholds[index] -
                          style.LightBandThresholds[index - 1U];
            } else {
                spacing = std::min(
                    style.LightBandThresholds[index] -
                        style.LightBandThresholds[index - 1U],
                    style.LightBandThresholds[index + 1U] -
                        style.LightBandThresholds[index]);
            }
        }
        spacing = std::max(spacing, 0.001F);
        const float delta = style.LightBandLevels[index + 1U] -
                            style.LightBandLevels[index];
        source += "    bandLuminance += " + Float(delta) +
                  " * smoothstep(" +
                  Float(style.LightBandThresholds[index]) +
                  " - OOT3D_TOON_SOFTNESS * " + Float(spacing) + ", " +
                  Float(style.LightBandThresholds[index]) +
                  " + OOT3D_TOON_SOFTNESS * " + Float(spacing) +
                  ", guide);\n";
    }
    source += "    return clamp(bandLuminance, 0.0, 1.0);\n}\n";
    return source;
}

std::string Helpers(const ToonStyleSettings& style) {
    std::string source = R"(
vec3 oot3d_toon_rotate_z(vec4 q) {
    float qlen = dot(q, q);
    if (qlen < 0.000001) return vec3(0.0, 0.0, 1.0);
    q *= inversesqrt(qlen);
    return vec3(2.0 * (q.x * q.z + q.w * q.y),
                2.0 * (q.y * q.z - q.w * q.x),
                1.0 - 2.0 * (q.x * q.x + q.y * q.y));
}
)";
    source += BandLuminanceHelper(style);
    source += R"(

vec3 oot3d_apply_toon(vec3 sourceColor) {
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    // Quantize the interpolated PICA lighting/color contribution, never the
    // post-TEV texel. This preserves albedo gradients and texture detail.
    float lightingGuide = clamp(dot(pica_primary_color.rgb, lumaWeights), 0.0, 1.0);
    float bandLuminance = oot3d_toon_band_luminance(lightingGuide);
    float lightingScale = clamp(bandLuminance / max(lightingGuide, 0.05),
                                0.0, 2.0);
    lightingScale = mix(1.0, lightingScale, OOT3D_TOON_HAS_VERTEX_LIGHT);
    vec3 banded = sourceColor * lightingScale;
    float bandedLuma = dot(banded, lumaWeights);
    banded = mix(vec3(bandedLuma), banded, OOT3D_TOON_SATURATION);
    float shadowMask = (1.0 - smoothstep(0.0, 0.55, bandLuminance)) *
                       OOT3D_TOON_HAS_VERTEX_LIGHT;
    banded = mix(banded, banded * OOT3D_TOON_SHADOW_TINT,
                 shadowMask * OOT3D_TOON_SHADOW_STRENGTH);
    vec3 normal = normalize(oot3d_toon_rotate_z(pica_normquat));
    vec3 viewDirection = dot(pica_view, pica_view) > 0.000001
        ? normalize(pica_view) : vec3(0.0, 0.0, 1.0);
    float rim = pow(clamp(1.0 - abs(dot(normal, viewDirection)), 0.0, 1.0),
                    OOT3D_TOON_RIM_WIDTH);
    return clamp(banded + OOT3D_TOON_RIM_TINT *
                 (rim * OOT3D_TOON_RIM_STRENGTH), 0.0, 1.0);
}

vec3 oot3d_material_bands(vec3 lighting, bool shadow_tint) {
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    float guide = clamp(dot(lighting, lumaWeights), 0.0, 1.0);
    float bandLuminance = oot3d_toon_band_luminance(guide);
    vec3 banded = lighting * clamp(bandLuminance / max(guide, 0.05), 0.0, 2.0);
    float bandedLuma = dot(banded, lumaWeights);
    banded = mix(vec3(bandedLuma), banded, OOT3D_TOON_SATURATION);
    if (shadow_tint) {
        float shadowMask = 1.0 - smoothstep(0.0, 0.55, bandLuminance);
        banded = mix(banded, banded * OOT3D_TOON_SHADOW_TINT,
                     shadowMask * OOT3D_TOON_SHADOW_STRENGTH);
    }
    return clamp(banded, 0.0, 1.0);
}

vec3 oot3d_material_rim(vec3 materialNormal) {
    vec3 normal = normalize(materialNormal);
    vec3 viewDirection = dot(pica_view, pica_view) > 0.000001
        ? normalize(pica_view) : vec3(0.0, 0.0, 1.0);
    float rim = pow(clamp(1.0 - abs(dot(normal, viewDirection)), 0.0, 1.0),
                    OOT3D_TOON_RIM_WIDTH);
    return OOT3D_TOON_RIM_TINT * (rim * OOT3D_TOON_RIM_STRENGTH);
}
)";
    return source;
}

std::string Declarations(const ToonStyleSettings& style,
                         bool usesVertexLighting) {
    std::string declarations;
    const uint8_t lightBands = std::clamp<uint8_t>(
        style.LightBands, 2U, kMaximumToonLightBands);
    declarations += "\n#define OOT3D_TOON_BAND_SPAN " +
                    Float(static_cast<float>(lightBands - 1U)) + "\n";
    declarations += "#define OOT3D_TOON_SOFTNESS " +
                    Float(style.BandSoftness) + "\n";
    declarations += "#define OOT3D_TOON_SATURATION " +
                    Float(style.Saturation) + "\n";
    declarations += std::string("#define OOT3D_TOON_HAS_VERTEX_LIGHT ") +
                    (usesVertexLighting ? "1.0\n" : "0.0\n");
    declarations += "#define OOT3D_TOON_SHADOW_TINT vec3(" +
                    Float(style.ShadowTint[0]) + ", " +
                    Float(style.ShadowTint[1]) + ", " +
                    Float(style.ShadowTint[2]) + ")\n";
    declarations += "#define OOT3D_TOON_SHADOW_STRENGTH " +
                    Float(style.ShadowStrength) + "\n";
    declarations += "#define OOT3D_TOON_RIM_STRENGTH " +
                    Float(style.RimStrength) + "\n";
    declarations += "#define OOT3D_TOON_RIM_WIDTH " +
                    Float(style.RimWidth) + "\n";
    declarations += "#define OOT3D_TOON_RIM_TINT vec3(" +
                    Float(style.RimTint[0]) + ", " +
                    Float(style.RimTint[1]) + ", " +
                    Float(style.RimTint[2]) + ")\n";
    declarations += Helpers(style);
    return declarations;
}

} // namespace

PicaToonEligibility ClassifyPicaToonDraw(const PicaToonDrawInfo& draw,
                                         ToonMode mode) {
    if (mode == ToonMode::Off) return PicaToonEligibility::Disabled;
    if (draw.CompositionDomain !=
        ::Oot3d::Renderer::PicaCompositionDomain::Scene)
        return PicaToonEligibility::OutsideScene;
    if (!draw.DepthTestEnabled || !draw.DepthWriteEnabled)
        return PicaToonEligibility::NoDepth;
    // OoT3D/PICA keeps fixed-function blending enabled for opaque world
    // geometry too. Depth writing is the reliable boundary: translucent
    // surfaces and UI draws do not write the scene depth buffer.
    if ((draw.ColorWriteMask & 0x7U) == 0U)
        return PicaToonEligibility::NoRgbOutput;
    return PicaToonEligibility::Eligible;
}

PicaToonShaderVariant BuildPicaToonShaderVariant(
    std::string_view source, uint64_t originalFragmentKey,
    const PicaToonDrawInfo& draw, ToonMode mode,
    const ToonStyleSettings& style) {
    PicaToonShaderVariant result;
    result.FragmentKey = originalFragmentKey;
    result.Eligibility = ClassifyPicaToonDraw(draw, mode);
    if (result.Eligibility != PicaToonEligibility::Eligible) return result;

    const size_t mainPosition = source.find("void main()");
    const size_t depthPosition = source.find(kDepthMarker);
    if (mainPosition == std::string_view::npos ||
        depthPosition == std::string_view::npos) {
        result.Eligibility = PicaToonEligibility::UnsupportedShader;
        return result;
    }

    const size_t primaryDeclaration = source.find("rounded_primary_color");
    const bool usesVertexLighting = primaryDeclaration != std::string_view::npos &&
        source.find("rounded_primary_color",
                    primaryDeclaration + std::string_view("rounded_primary_color").size()) !=
            std::string_view::npos;
    const size_t materialMarker = source.find(kMaterialMarker);
    const bool materialPath = mode == ToonMode::PicaMaterial &&
                              materialMarker != std::string_view::npos;
    result.MaterialPath = materialPath;
    const std::string declarations =
        Declarations(style, usesVertexLighting);

    result.Source.reserve(source.size() + declarations.size() + 256U);
    result.Source.append(source.substr(0, mainPosition));
    result.Source += declarations;
    if (materialPath) {
        const size_t insertion = materialMarker + kMaterialMarker.size();
        result.Source.append(source.substr(mainPosition, insertion - mainPosition));
        result.Source +=
            "\n    primary_fragment_color.rgb = oot3d_material_bands(primary_fragment_color.rgb, true);\n"
            "    secondary_fragment_color.rgb = clamp(oot3d_material_bands(secondary_fragment_color.rgb, false) + oot3d_material_rim(normal), 0.0, 1.0);";
        result.Source.append(source.substr(insertion));
    } else {
        result.Source.append(source.substr(mainPosition,
                                           depthPosition - mainPosition));
        result.Source += "    combiner_output.rgb = oot3d_apply_toon(combiner_output.rgb);\n";
        result.Source.append(source.substr(depthPosition));
    }
    result.FragmentKey = VariantKey(originalFragmentKey, mode, style);
    return result;
}

PicaToonInstrumentation BuildPicaToonInstrumentation(
    uint64_t originalFragmentKey, const PicaToonDrawInfo& draw,
    ToonMode mode, const ToonStyleSettings& style,
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks) {
    using ::Oot3d::Renderer::PicaShaderHook;
    using ::Oot3d::Renderer::PicaShaderSemantic;

    PicaToonInstrumentation result;
    result.FragmentKey = originalFragmentKey;
    result.Eligibility = ClassifyPicaToonDraw(draw, mode);
    if (result.Eligibility != PicaToonEligibility::Eligible) {
        return result;
    }

    const bool commonSemantics =
        hooks.Valid() &&
        hooks.Has(PicaShaderSemantic::NormalQuaternion) &&
        hooks.Has(PicaShaderSemantic::PrimaryColorInput) &&
        hooks.Has(PicaShaderSemantic::ViewVector) &&
        hooks.Has(PicaShaderSemantic::CombinerOutput);
    const bool materialPath =
        mode == ToonMode::PicaMaterial && commonSemantics &&
        hooks.Supports(PicaShaderHook::PicaLighting) &&
        hooks.Has(PicaShaderSemantic::MaterialLightingPoint) &&
        hooks.Has(PicaShaderSemantic::SecondaryFragmentColor) &&
        hooks.Has(PicaShaderSemantic::MaterialNormal);
    if (!materialPath &&
        (!commonSemantics ||
         !hooks.Supports(PicaShaderHook::BeforeDepth))) {
        result.Eligibility = PicaToonEligibility::UnsupportedShader;
        return result;
    }

    result.MaterialPath = materialPath;
    result.InsertionHook = materialPath
                               ? PicaShaderHook::PicaLighting
                               : PicaShaderHook::BeforeDepth;
    result.Declarations = Declarations(
        style, hooks.Has(PicaShaderSemantic::PrimaryColorConsumed));
    if (materialPath) {
        result.Body =
            "\n    primary_fragment_color.rgb = "
            "oot3d_material_bands(primary_fragment_color.rgb, true);\n"
            "    secondary_fragment_color.rgb = clamp("
            "oot3d_material_bands(secondary_fragment_color.rgb, false) + "
            "oot3d_material_rim(normal), 0.0, 1.0);";
    } else {
        result.Body =
            "    combiner_output.rgb = "
            "oot3d_apply_toon(combiner_output.rgb);\n";
    }
    result.FragmentKey = VariantKey(originalFragmentKey, mode, style);
    return result;
}

} // namespace Fast::Oot3d
