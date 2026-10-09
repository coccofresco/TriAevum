#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/renderer3ds/point_shadow_map.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "fast/renderer3ds/pica_material_guide.h"

#include <algorithm>
#include <charconv>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr std::string_view kMain = "void main()";
constexpr std::string_view kDepthMarker = "    float pica_z_over_w = -gl_FragCoord.z;";
constexpr std::string_view kNativeColorMarker = "    pica_color =";
constexpr std::string_view kMaterialLightingMarker = "    // OOT3D_PICA_MATERIAL_TOON_POINT";
constexpr std::string_view kVertexLightingMarker =
    "    // OOT3D_PICA_LIGHTING_EXTENSION_POINT";
constexpr std::string_view kAmbientReadyMarker = "// OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY";
constexpr std::string_view kSpecularGuideStart = "    float oot3d_specular_signal = clamp(max(max("
                                                 "secondary_fragment_color.r, secondary_fragment_color.g), "
                                                 "secondary_fragment_color.b) * 4.0, 0.0, 1.0);\n";

struct Insertion {
    size_t Position = PicaShaderHookLayout::Unavailable;
    std::string Source;

    [[nodiscard]] bool Valid() const noexcept {
        return Position != PicaShaderHookLayout::Unavailable &&
               !Source.empty();
    }
};

bool IsIdentifierCharacter(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_';
}

bool ContainsIdentifier(std::string_view source, std::string_view identifier) noexcept {
    size_t offset = 0;
    while ((offset = source.find(identifier, offset)) != std::string_view::npos) {
        const bool leftBoundary = offset == 0U || !IsIdentifierCharacter(source[offset - 1U]);
        const size_t end = offset + identifier.size();
        const bool rightBoundary = end == source.size() || !IsIdentifierCharacter(source[end]);
        if (leftBoundary && rightBoundary) {
            return true;
        }
        offset = end;
    }
    return false;
}

void AnalyzeTextureSamples(PicaShaderHookLayout& layout,
                           std::string_view mainBody) {
    std::string compact;
    compact.reserve(mainBody.size());
    for (const char value : mainBody) {
        if (std::isspace(static_cast<unsigned char>(value)) == 0) {
            compact.push_back(value);
        }
    }

    for (uint8_t texture = 0U;
         texture < layout.TextureSamples.size(); ++texture) {
        const std::string textureName = std::to_string(texture);
        for (uint8_t coordinate = 0U; coordinate < 3U; ++coordinate) {
            const std::string coordinateName = std::to_string(coordinate);
            const std::string arguments =
                "vec2(pica_texcoord" + coordinateName +
                ".x,1.0-pica_texcoord" + coordinateName + ".y)";
            if (compact.find("pica_sample_texture" + textureName +
                             "(" + arguments + ")") ==
                    std::string::npos &&
                compact.find("texture(pica_texture" + textureName +
                             "," + arguments + ")") ==
                    std::string::npos) {
                continue;
            }
            layout.SampledTextureMask |=
                static_cast<uint8_t>(1U << texture);
            layout.TextureSamples[texture] = {
                coordinate,
                PicaTextureCoordinateOperation::NativeVFlip};
            break;
        }
    }

    constexpr std::string_view projectedTexture0 =
        "textureProj(pica_texture0,vec3(pica_texcoord0.x,"
        "pica_texcoord0_w-pica_texcoord0.y,pica_texcoord0_w))";
    if (compact.find(projectedTexture0) != std::string::npos) {
        layout.SampledTextureMask |= 1U;
        layout.TextureSamples[0] = {
            0U,
            PicaTextureCoordinateOperation::ProjectedNativeVFlip};
    }
}

bool DeclarationContainsToken(std::string_view declaration, std::string_view token) noexcept {
    return ContainsIdentifier(declaration, token);
}

std::optional<uint32_t> ParseLayoutLocation(std::string_view qualifiers) noexcept {
    std::string compact;
    compact.reserve(qualifiers.size());
    for (const char value : qualifiers) {
        if (std::isspace(static_cast<unsigned char>(value)) == 0) {
            compact.push_back(value);
        }
    }
    constexpr std::string_view marker = "location=";
    const size_t markerOffset = compact.find(marker);
    if (markerOffset == std::string::npos) {
        return std::nullopt;
    }
    size_t cursor = markerOffset + marker.size();
    uint32_t location = 0;
    bool digit = false;
    while (cursor < compact.size() && std::isdigit(static_cast<unsigned char>(compact[cursor])) != 0) {
        digit = true;
        location = location * 10U + static_cast<uint32_t>(compact[cursor] - '0');
        ++cursor;
    }
    if (!digit || (cursor < compact.size() && compact[cursor] != ',')) {
        return std::nullopt;
    }
    return location;
}

bool HasOutputAtLocation(std::string_view source, uint32_t location, std::string_view name = {}) noexcept {
    size_t cursor = 0U;
    while ((cursor = source.find("layout", cursor)) != std::string_view::npos) {
        const size_t open = source.find('(', cursor + 6U);
        const size_t close = open == std::string_view::npos ? std::string_view::npos : source.find(')', open + 1U);
        if (close == std::string_view::npos) {
            return false;
        }
        const auto declaredLocation = ParseLayoutLocation(source.substr(open + 1U, close - open - 1U));
        const size_t declarationEnd = source.find(';', close + 1U);
        if (declarationEnd == std::string_view::npos) {
            return false;
        }
        const std::string_view declaration = source.substr(close + 1U, declarationEnd - close - 1U);
        if (declaredLocation == location && DeclarationContainsToken(declaration, "out") &&
            (name.empty() || DeclarationContainsToken(declaration, name))) {
            return true;
        }
        cursor = declarationEnd + 1U;
    }
    return false;
}

size_t FindMainClosingBrace(std::string_view source, size_t openingBrace) noexcept {
    enum class ScanState : uint8_t {
        Code,
        LineComment,
        BlockComment,
        String,
    };
    ScanState state = ScanState::Code;
    size_t depth = 0U;
    bool escaped = false;
    for (size_t cursor = openingBrace; cursor < source.size(); ++cursor) {
        const char value = source[cursor];
        const char next = cursor + 1U < source.size() ? source[cursor + 1U] : '\0';
        switch (state) {
            case ScanState::LineComment:
                if (value == '\n')
                    state = ScanState::Code;
                continue;
            case ScanState::BlockComment:
                if (value == '*' && next == '/') {
                    state = ScanState::Code;
                    ++cursor;
                }
                continue;
            case ScanState::String:
                if (!escaped && value == '"')
                    state = ScanState::Code;
                escaped = !escaped && value == '\\';
                if (value != '\\')
                    escaped = false;
                continue;
            case ScanState::Code:
                break;
        }
        if (value == '/' && next == '/') {
            state = ScanState::LineComment;
            ++cursor;
        } else if (value == '/' && next == '*') {
            state = ScanState::BlockComment;
            ++cursor;
        } else if (value == '"') {
            state = ScanState::String;
            escaped = false;
        } else if (value == '{') {
            ++depth;
        } else if (value == '}') {
            if (depth == 0U)
                return PicaShaderHookLayout::Unavailable;
            --depth;
            if (depth == 0U)
                return cursor;
        }
    }
    return PicaShaderHookLayout::Unavailable;
}

uint64_t SaltedVariantKey(uint64_t original, std::string_view salt) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<uint8_t>(original >> shift);
        hash *= 1099511628211ULL;
    }
    for (const char value : salt) {
        hash ^= static_cast<uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t AmbientVariantKey(uint64_t original) noexcept {
    return SaltedVariantKey(original, "OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_V3");
}

uint64_t NormalGuideVariantKey(uint64_t original) noexcept {
    return SaltedVariantKey(original, "OOT3D_PICA_NORMAL_GUIDE_V1");
}

uint64_t SceneDomainVariantKey(uint64_t original, const PicaSceneDomainGuideFeatures& features, bool blended) noexcept {
    uint64_t hash = SaltedVariantKey(original, "OOT3D_PICA_SCENE_DOMAIN_GUIDE_V1");
    const auto mix = [&hash](uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(features.AmbientOcclusion ? 1U : 0U);
    mix(features.Outline ? 1U : 0U);
    mix(blended ? 1U : 0U);
    if (features.GlobalIllumination) mix(3U);
    return hash;
}

void MixKey(uint64_t& key, uint32_t value) noexcept {
    key ^= value;
    key *= 1099511628211ULL;
}

ReflectionMaterialParameters SanitizeProfile(const ReflectionMaterialParameters& profile) noexcept {
    const auto finiteClamp = [](float value, float minimum, float maximum, float fallback) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    return {
        finiteClamp(profile.Reflectivity, 0.0F, 1.0F, 0.0F),
        finiteClamp(profile.Roughness, 0.02F, 1.0F, 1.0F),
    };
}

// The value a fixed six-decimal GLSL literal of value parses to.
float LiteralValue(float value) noexcept {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << value;
    const auto text = stream.str();
    float parsed = 0.0F;
    std::from_chars(text.data(), text.data() + text.size(), parsed);
    return parsed;
}

std::string BuildExplicitMaterialGuide(const ReflectionMaterialParameters& profile) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << "pica_material_guide = vec4(" << profile.Reflectivity << ", "
           << profile.Roughness << ", (127.0 / 255.0), 0.0); // OOT3D explicit texture material";
    return stream.str();
}

std::string BuildCalibratedMaterialGuide() {
    return "    float oot3d_specular_peak = max(max("
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
           "(127.0 / 255.0), 0.0); // OOT3D calibrated specular material\n";
}

std::string Compose(std::string_view source,
                    const PicaShaderHookLayout& hooks,
                    std::string_view declarations,
                    std::string_view epilogue,
                    const std::vector<Insertion>& insertions) {
    const size_t global = hooks.Offset(PicaShaderHook::GlobalDeclarations);
    const size_t end = hooks.Offset(PicaShaderHook::MainEpilogue);
    if (global > end || end > source.size()) {
        return {};
    }

    struct Edit {
        size_t Begin = 0U;
        size_t End = 0U;
        std::string_view Source;
    };
    std::vector<Edit> edits;
    edits.reserve(insertions.size());
    size_t insertionBytes = 0U;
    for (const auto& insertion : insertions) {
        if (!insertion.Valid()) {
            continue;
        }
        edits.push_back({insertion.Position, insertion.Position,
                         insertion.Source});
        insertionBytes += insertion.Source.size();
    }
    std::stable_sort(
        edits.begin(), edits.end(),
        [](const Edit& left, const Edit& right) {
            return left.Begin < right.Begin;
        });
    size_t checkedCursor = global;
    for (const auto& edit : edits) {
        if (edit.Begin < checkedCursor || edit.End < edit.Begin ||
            edit.End > end) {
            return {};
        }
        checkedCursor = edit.End;
    }

    std::string result;
    result.reserve(source.size() + declarations.size() +
                   epilogue.size() + insertionBytes);
    result.append(source.substr(0U, global));
    result.append(declarations);
    size_t cursor = global;
    for (const auto& edit : edits) {
        result.append(source.substr(cursor, edit.Begin - cursor));
        result.append(edit.Source);
        cursor = edit.End;
    }
    result.append(source.substr(cursor, end - cursor));
    result.append(epilogue);
    result.append(source.substr(end));
    return result;
}

} // namespace

std::string PicaTextureReliefDeclarations(std::string_view gain) {
    return R"glsl(
// Relative contrast (log2 luminance): a dark texture's detail carries the
// same relief as a bright one's; the offset bounds near-black texels.
float oot3d_relief_height(vec4 texel) {
    return log2(dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722)) + 0.0625);
}
// Bends the guide normal against the colour texture's luminance slope (per
// texel along the texture's u and v axes) in the surface's tangent plane.
vec3 oot3d_relief_normal(vec3 n, vec2 slope, vec3 dpdx, vec3 dpdy, vec2 duvdx, vec2 duvdy) {
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    float length2 = dot(n, n);
    if (abs(determinant) <= 1.0e-20 || length2 <= 1.0e-12) return n;
    n *= inversesqrt(length2);
    vec3 axis_u = (dpdx * duvdy.y - dpdy * duvdx.y) * sign(determinant);
    vec3 axis_v = (dpdy * duvdx.x - dpdx * duvdy.x) * sign(determinant);
    axis_u -= n * dot(n, axis_u);
    axis_v -= n * dot(n, axis_v);
    if (dot(axis_u, axis_u) <= 1.0e-20 || dot(axis_v, axis_v) <= 1.0e-20) return n;
    return normalize(n - )glsl" + std::string(gain) + R"glsl( *
        (slope.x * normalize(axis_u) + slope.y * normalize(axis_v)));
}
)glsl";
}

std::string PicaTextureReliefPrologue(std::string_view unitText, std::string_view coordinateText) {
    const std::string unit(unitText), coordinate(coordinateText);
    return "    vec2 oot3d_relief_uv = vec2(" + coordinate + ".x, 1.0 - " + coordinate + ".y);\n"
        "    vec2 oot3d_relief_size = vec2(textureSize(pica_texture" + unit + ", 0));\n"
        "    float oot3d_relief_lod = max(pica_texture_lod(oot3d_relief_uv, oot3d_relief_size) +\n"
        "        fragment_uniforms.texture_lod_bias[" + unit + "], 0.0);\n"
        "    vec2 oot3d_relief_step = exp2(oot3d_relief_lod) / oot3d_relief_size;\n"
        "    vec2 oot3d_relief_slope = 0.5 * vec2(\n"
        "        oot3d_relief_height(textureLod(pica_texture" + unit +
            ", oot3d_relief_uv + vec2(oot3d_relief_step.x, 0.0), oot3d_relief_lod)) -\n"
        "        oot3d_relief_height(textureLod(pica_texture" + unit +
            ", oot3d_relief_uv - vec2(oot3d_relief_step.x, 0.0), oot3d_relief_lod)),\n"
        "        oot3d_relief_height(textureLod(pica_texture" + unit +
            ", oot3d_relief_uv + vec2(0.0, oot3d_relief_step.y), oot3d_relief_lod)) -\n"
        "        oot3d_relief_height(textureLod(pica_texture" + unit +
            ", oot3d_relief_uv - vec2(0.0, oot3d_relief_step.y), oot3d_relief_lod)));\n"
        "    vec3 oot3d_relief_dpdx = -dFdx(pica_view), oot3d_relief_dpdy = -dFdy(pica_view);\n"
        "    vec2 oot3d_relief_duvdx = dFdx(oot3d_relief_uv), oot3d_relief_duvdy = dFdy(oot3d_relief_uv);\n";
}

::Oot3d::Renderer::PicaFragmentOutputContract
AnalyzePicaFragmentOutputContract(std::string_view source) noexcept {
    using ::Oot3d::Renderer::PicaFragmentDepthOutput;
    using ::Oot3d::Renderer::PicaFragmentOutputContract;

    PicaFragmentOutputContract result;
    result.SchemaVersion =
        ::Oot3d::Renderer::kPicaFragmentOutputContractSchemaVersion;
    result.Depth = ContainsIdentifier(source, "gl_FragDepth")
        ? PicaFragmentDepthOutput::ExplicitNative
        : PicaFragmentDepthOutput::FixedFunction;

    size_t cursor = 0U;
    while ((cursor = source.find("layout", cursor)) !=
           std::string_view::npos) {
        const size_t open = source.find('(', cursor + 6U);
        const size_t close =
            open == std::string_view::npos
                ? std::string_view::npos
                : source.find(')', open + 1U);
        if (close == std::string_view::npos) {
            break;
        }
        const auto location = ParseLayoutLocation(
            source.substr(open + 1U, close - open - 1U));
        const size_t declarationEnd = source.find(';', close + 1U);
        if (declarationEnd == std::string_view::npos) {
            break;
        }
        const std::string_view declaration = source.substr(
            close + 1U, declarationEnd - close - 1U);
        if (location.has_value() &&
            DeclarationContainsToken(declaration, "out")) {
            if (*location >= 32U) {
                result.UnsupportedColorLocation = true;
            } else {
                const uint32_t locationBit = 1U << *location;
                if ((result.ColorLocationMask & locationBit) != 0U) {
                    result.DuplicateColorLocation = true;
                }
                result.ColorLocationMask |= locationBit;
            }
            if (DeclarationContainsToken(declaration, "pica_color")) {
                if (result.NativeColorLocation !=
                    PicaFragmentOutputContract::UnavailableLocation) {
                    result.DuplicateColorLocation = true;
                } else if (*location < 32U) {
                    result.NativeColorLocation =
                        static_cast<uint8_t>(*location);
                } else {
                    result.UnsupportedColorLocation = true;
                }
            }
        }
        cursor = declarationEnd + 1U;
    }
    return result;
}

PicaShaderHookLayout AnalyzePicaFragmentShaderHooks(std::string_view source) noexcept {
    PicaShaderHookLayout result;
    result.SchemaVersion =
        ::Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    result.SourceSize = source.size();
    result.Outputs = AnalyzePicaFragmentOutputContract(source);
    const size_t main = source.find(kMain);
    if (main == std::string_view::npos)
        return result;
    const size_t openingBrace = source.find('{', main + kMain.size());
    if (openingBrace == std::string_view::npos)
        return result;
    const size_t closingBrace = FindMainClosingBrace(source, openingBrace);
    if (closingBrace == PicaShaderHookLayout::Unavailable)
        return result;

    result.Offsets[static_cast<size_t>(PicaShaderHook::GlobalDeclarations)] = main;
    result.Offsets[static_cast<size_t>(PicaShaderHook::MainPrologue)] = openingBrace + 1U;
    result.Offsets[static_cast<size_t>(PicaShaderHook::MainEpilogue)] = closingBrace;
    AnalyzeTextureSamples(
        result,
        source.substr(openingBrace + 1U,
                      closingBrace - openingBrace - 1U));

    const size_t lighting = source.find(kMaterialLightingMarker, openingBrace);
    if (lighting != std::string_view::npos && lighting < closingBrace) {
        result.Offsets[static_cast<size_t>(PicaShaderHook::PicaLighting)] = lighting + kMaterialLightingMarker.size();
        result.Semantics |= PicaShaderSemantic::MaterialLightingPoint;
    } else {
        size_t vertexLighting =
            source.find(kVertexLightingMarker, openingBrace);
        size_t vertexLightingOffset = PicaShaderHookLayout::Unavailable;
        if (vertexLighting != std::string_view::npos &&
            vertexLighting < closingBrace) {
            vertexLightingOffset =
                vertexLighting + kVertexLightingMarker.size();
        } else {
            constexpr std::string_view declaration =
                "vec4 rounded_primary_color";
            vertexLighting = source.find(declaration, openingBrace);
            const size_t semicolon =
                vertexLighting == std::string_view::npos
                    ? std::string_view::npos
                    : source.find(';', vertexLighting + declaration.size());
            if (semicolon != std::string_view::npos &&
                semicolon < closingBrace) {
                vertexLightingOffset = semicolon + 1U;
            }
        }
        if (vertexLightingOffset != PicaShaderHookLayout::Unavailable) {
            result.Offsets[static_cast<size_t>(
                PicaShaderHook::PicaLighting)] = vertexLightingOffset;
            result.Semantics |= PicaShaderSemantic::VertexLightingPoint;
        }
    }
    const size_t depth = source.find(kDepthMarker, openingBrace);
    if (depth != std::string_view::npos && depth < closingBrace) {
        result.Offsets[static_cast<size_t>(PicaShaderHook::BeforeDepth)] = depth;
    }
    const size_t color = source.find(kNativeColorMarker, openingBrace);
    if (color != std::string_view::npos && color < closingBrace) {
        result.Offsets[static_cast<size_t>(PicaShaderHook::BeforeNativeColor)] = color;
    }

    if (ContainsIdentifier(source, "pica_normquat")) {
        result.Semantics |= PicaShaderSemantic::NormalQuaternion;
    }
    if (HasOutputAtLocation(source, 0U, "pica_color")) {
        result.Semantics |= PicaShaderSemantic::NativeColorOutput;
    }
    if (HasOutputAtLocation(source, 1U, "pica_normal_guide")) {
        result.Semantics |= PicaShaderSemantic::NormalGuideOutput;
    }
    if (ContainsIdentifier(source, "fog_factor") && ContainsIdentifier(source, "fragment_uniforms")) {
        result.Semantics |= PicaShaderSemantic::NativeFogFactor;
    }
    if (HasOutputAtLocation(source, 2U, "pica_material_guide")) {
        result.Semantics |= PicaShaderSemantic::MaterialGuideOutput;
    }
    if (HasOutputAtLocation(source, 4U, "pica_ambient_guide")) {
        result.Semantics |= PicaShaderSemantic::AmbientGuideOutput;
    }
    if (source.find(kAmbientReadyMarker) != std::string_view::npos &&
        ContainsIdentifier(source, "oot3d_ao_response_rgb")) {
        result.Semantics |= PicaShaderSemantic::AmbientOcclusionResponse;
    }
    if (ContainsIdentifier(source, "secondary_fragment_color")) {
        result.Semantics |= PicaShaderSemantic::SecondaryFragmentColor;
        const size_t declaration =
            source.find("vec4 secondary_fragment_color", openingBrace);
        const size_t declarationEnd =
            declaration == std::string_view::npos
                ? std::string_view::npos
                : source.find(';', declaration);
        const size_t legacyGuide = source.find(
            kSpecularGuideStart,
            declarationEnd == std::string_view::npos
                ? openingBrace
                : declarationEnd + 1U);
        const size_t consumptionEnd =
            legacyGuide != std::string_view::npos &&
                    legacyGuide < closingBrace
                ? legacyGuide
                : closingBrace;
        if (declarationEnd != std::string_view::npos &&
            declarationEnd < consumptionEnd &&
            ContainsIdentifier(
                source.substr(declarationEnd + 1U,
                              consumptionEnd - declarationEnd - 1U),
                "secondary_fragment_color")) {
            result.Semantics |= PicaShaderSemantic::
                SecondaryFragmentColorConsumed;
        }
    }
    if (ContainsIdentifier(source, "pica_primary_color")) {
        result.Semantics |= PicaShaderSemantic::PrimaryColorInput;
    }
    if (ContainsIdentifier(source, "pica_view")) {
        result.Semantics |= PicaShaderSemantic::ViewVector;
    }
    if (ContainsIdentifier(source, "combiner_output")) {
        result.Semantics |= PicaShaderSemantic::CombinerOutput;
    }
    const size_t roundedPrimary = source.find("rounded_primary_color");
    if (roundedPrimary != std::string_view::npos &&
        source.find("rounded_primary_color",
                    roundedPrimary +
                        std::string_view("rounded_primary_color").size()) !=
            std::string_view::npos) {
        result.Semantics |= PicaShaderSemantic::PrimaryColorConsumed;
    }
    if (ContainsIdentifier(source, "normal")) {
        result.Semantics |= PicaShaderSemantic::MaterialNormal;
    }
    return result;
}

PicaFragmentInstrumentationResult
BuildPicaFragmentInstrumentationVariant(const PicaFragmentInstrumentationRequest& request) {
    PicaFragmentInstrumentationResult result;
    result.FragmentKey = request.FragmentKey;
    if (request.Hooks != nullptr && request.Hooks->ValidFor(request.Source)) {
        result.Hooks = *request.Hooks;
        result.UsedProvidedHooks = true;
    } else {
        result.Hooks = AnalyzePicaFragmentShaderHooks(request.Source);
    }

    std::string declarations;
    std::string epilogue;
    std::string deferredEpilogue;
    std::vector<Insertion> insertions;
    const bool surfaceLighting = HasPicaShaderInstrumentationFeature(
        request.RequestedFeatures, PicaShaderInstrumentationFeature::NativePerPixelLighting) &&
        result.UsedProvidedHooks && result.Hooks.Supports(PicaShaderHook::PicaLighting) &&
        result.Hooks.Supports(PicaShaderHook::AfterNativeTev) &&
        result.Hooks.Offset(PicaShaderHook::AfterNativeTev) >= result.Hooks.Offset(PicaShaderHook::PicaLighting) &&
        result.Hooks.Offset(PicaShaderHook::AfterNativeTev) <= request.Source.size() &&
        result.Hooks.Has(PicaShaderSemantic::PrimaryColorInput) &&
        result.Hooks.Has(PicaShaderSemantic::ViewVector);
    const auto pbr = surfaceLighting && HasPicaShaderInstrumentationFeature(
        request.RequestedFeatures, PicaShaderInstrumentationFeature::PbrMaterial)
        ? Renderer3ds::BuildPicaPbrShader(result.Hooks, request.Pbr) : std::nullopt;
    using Decision = PicaEffectDecisions;
    auto& decisions = result.Decisions;
    if (AnalyzePicaFragmentOutputContract(request.Source).Depth ==
        ::Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative)
        decisions.Set(Decision::ExplicitDepth);
    if (surfaceLighting) {
        decisions.Set(Decision::SurfaceLighting);
        declarations += Renderer3ds::PicaSurfaceLightingDeclarations();
        insertions.push_back({result.Hooks.Offset(PicaShaderHook::PicaLighting),
            "\n    vec4 surface_native_primary = rounded_primary_color;\n"
            "    vec4 surface_native_fragment = primary_fragment_color;\n"
            "    vec4 surface_native_secondary = secondary_fragment_color;\n" +
            (pbr ? pbr->UnlitInputs : std::string(Renderer3ds::PicaSurfaceLightingBody()))});
        // Native TEV alpha may read RGB operands or DOT3_RGBA. Re-evaluate
        // that dependency chain with the original lighting; never let an
        // extension change native coverage. Dead RGB outputs are eliminated
        // by the shader compiler. The frontend owns both region boundaries.
        const auto tevBegin = result.Hooks.Offset(PicaShaderHook::PicaLighting);
        const auto tevEnd = result.Hooks.Offset(PicaShaderHook::AfterNativeTev);
        std::string preserveAlpha = R"glsl(
    {
        vec3 surface_lit_rgb = combiner_output.rgb;
        vec4 rounded_primary_color = surface_native_primary;
        vec4 primary_fragment_color = surface_native_fragment;
        vec4 secondary_fragment_color = surface_native_secondary;
        vec4 combiner_buffer = vec4(0.0);
        vec4 next_combiner_buffer = fragment_uniforms.combiner_buffer_color;
        combiner_output = vec4(0.0);
)glsl";
        if (pbr) preserveAlpha += pbr->NativeCoordinates;
        preserveAlpha.append(request.Source.substr(tevBegin, tevEnd - tevBegin));
        preserveAlpha += "\n        combiner_output.rgb = surface_lit_rgb;\n    }\n";
        insertions.push_back({tevEnd, std::move(preserveAlpha)});
        if (!pbr) insertions.push_back({tevEnd, std::string(Renderer3ds::PicaSurfaceLightingWaterShading())});
        result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_NATIVE_PER_PIXEL_LIGHTING_V20_WATER_NATIVE_FOG");
        result.AppliedFeatures |= PicaShaderInstrumentationFeature::NativePerPixelLighting;
        if (pbr) {
            declarations += pbr->Declarations;
            insertions.push_back({tevEnd, pbr->ShadingBody});
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_PBR_GGX_V12_GI_SOURCES");
            MixKey(result.FragmentKey, request.Pbr.TextureSlot);
            MixKey(result.FragmentKey, request.Pbr.NativeFragmentLighting);
            MixKey(result.FragmentKey, request.Pbr.NativeAttenuationLuts);
            MixKey(result.FragmentKey, request.Pbr.SeamSoftening);
            result.AppliedFeatures |= PicaShaderInstrumentationFeature::PbrMaterial;
            const auto* sample = result.Hooks.TextureSample(request.Pbr.TextureSlot);
            decisions.Set(Decision::Pbr);
            decisions.PbrCoordinate = sample->Coordinate;
            decisions.PbrProjected = sample->Operation == PicaTextureCoordinateOperation::ProjectedNativeVFlip;
            decisions.PbrNativeLighting = request.Pbr.NativeFragmentLighting;
            decisions.PbrAttenuationLuts = request.Pbr.NativeAttenuationLuts;
            decisions.PbrSeams = request.Pbr.SeamSoftening;
        }
        if (result.Hooks.Has(PicaShaderSemantic::NativeColorOutput))
            insertions.push_back({tevEnd, "\n    vec3 surface_before_fog = combiner_output.rgb;\n"});
        if (!pbr && HasPicaShaderInstrumentationFeature(
                request.RequestedFeatures, PicaShaderInstrumentationFeature::DiffuseAlbedoGuide)) {
            // Diffuse albedo: the native TEV under unit lighting (primary 1,
            // secondary 0), i.e. textures times material colour without the
            // lighting. The lit result is restored untouched.
            decisions.Set(Decision::AlbedoReplay);
            std::string albedo = R"glsl(
    vec3 surface_gi_albedo;
    {
        vec4 surface_gi_saved = combiner_output;
        vec4 rounded_primary_color = vec4(vec3(1.0), surface_native_primary.a);
        vec4 primary_fragment_color = vec4(vec3(1.0), surface_native_fragment.a);
        vec4 secondary_fragment_color = vec4(vec3(0.0), surface_native_secondary.a);
        vec4 combiner_buffer = vec4(0.0);
        vec4 next_combiner_buffer = fragment_uniforms.combiner_buffer_color;
        combiner_output = vec4(0.0);
)glsl";
            albedo.append(request.Source.substr(tevBegin, tevEnd - tevBegin));
            albedo += "\n        surface_gi_albedo = clamp(combiner_output.rgb, 0.0, 1.0);\n"
                      "        combiner_output = surface_gi_saved;\n    }\n";
            insertions.push_back({tevEnd, std::move(albedo)});
        }
    }
    bool normalAvailable = result.Hooks.Has(PicaShaderSemantic::NormalGuideOutput);
    bool ambientAvailable = result.Hooks.Has(PicaShaderSemantic::AmbientGuideOutput);
    bool materialAvailable = result.Hooks.Has(PicaShaderSemantic::MaterialGuideOutput);
    // PICA often enables blending even for opaque ONE/ZERO draws. Conversely,
    // writing depth does not make an alpha-composited surface opaque.
    const auto& blend = request.Draw.Blend;
    const bool replacesDestinationRgb = !blend.Enabled ||
        (blend.EquationRgb == ::Oot3d::Renderer::NativeBlendEquation::Add &&
         blend.SourceRgb == ::Oot3d::Renderer::NativeBlendFactor::One &&
         blend.DestRgb == ::Oot3d::Renderer::NativeBlendFactor::Zero);
    const bool rigidMotionWillApply =
        HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::RigidMotionGuide) &&
        result.Hooks.Valid() &&
        !ContainsIdentifier(request.Source, "pica_rigid_motion_guide") &&
        !HasOutputAtLocation(request.Source, 3U);

    bool viewDerivativesInserted = false;
    const auto ensureNormalGuide = [&](Decision::NormalWriter writer) {
        if (normalAvailable) {
            return true;
        }
        const bool materialNormal =
            result.Hooks.Has(PicaShaderSemantic::MaterialNormal);
        const bool normalQuaternion =
            result.Hooks.Has(PicaShaderSemantic::NormalQuaternion);
        if (!result.Hooks.Valid() ||
            (!surfaceLighting && !materialNormal && !normalQuaternion) ||
            HasOutputAtLocation(request.Source, 1U)) {
            return false;
        }
        declarations +=
            "layout(location=1) out vec4 pica_normal_guide;\n";
        const bool typedNormal = !surfaceLighting && !materialNormal && request.SurfaceNormalInput;
        decisions.Set(Decision::NormalGuide);
        decisions.NormalAt = writer;
        decisions.Normal = pbr ? Decision::NormalPath::Pbr
            : surfaceLighting ? Decision::NormalPath::Surface
            : materialNormal ? Decision::NormalPath::Material
            : typedNormal ? Decision::NormalPath::Typed
            : result.Hooks.Has(PicaShaderSemantic::ViewVector) ? Decision::NormalPath::NativeView
            : Decision::NormalPath::NativeRotate;
        if (pbr) {
            epilogue += "    pica_normal_guide = vec4(pbr_normal * 0.5 + 0.5, 1.0);\n";
        } else if (surfaceLighting) {
            epilogue +=
                "    pica_normal_guide = vec4(pica_surface_unit(pica_surface_normal) * 0.5 + 0.5, 1.0);\n";
        } else if (materialNormal) {
            epilogue +=
                "    pica_normal_guide = vec4(normal * 0.5 + 0.5, 1.0);\n";
        } else {
            if (typedNormal)
                declarations += "layout(location=9) in vec3 pica_surface_normal;\n";
            declarations += R"glsl(
vec3 oot3d_normal_guide_rotate_z(vec4 q) {
    float qlen = dot(q, q);
    if (qlen < 0.000001) return vec3(0.0, 0.0, 1.0);
    q *= inversesqrt(qlen);
    return vec3(2.0 * (q.x * q.z + q.w * q.y),
                2.0 * (q.y * q.z - q.w * q.x),
                1.0 - 2.0 * (q.x * q.x + q.y * q.y));
}
)glsl";
            // The view derivatives are taken before the native alpha test can
            // end an invocation of the quad (they are undefined after it).
            const bool prologueDerivatives = result.Hooks.Supports(PicaShaderHook::MainPrologue);
            if (result.Hooks.Has(PicaShaderSemantic::ViewVector)) {
                declarations += prologueDerivatives ? kNormalGuideSurfaceFromDerivatives : R"glsl(
vec3 oot3d_normal_guide_surface(vec4 q, vec3 to_eye) {
    // Vertex-lit PICA programs can disable the normal-quaternion output.
    // Their view vector still describes geometry in the same camera basis.
    vec3 geometric = cross(dFdx(to_eye), dFdy(to_eye));
    float area = dot(geometric, geometric);
    if (dot(q, q) >= 0.000001) return oot3d_normal_guide_rotate_z(q);
    if (area <= 1.0e-20) return vec3(0.0, 0.0, 1.0);
    geometric *= inversesqrt(area);
    return dot(geometric, to_eye) < 0.0 ? -geometric : geometric;
}
)glsl";
                if (prologueDerivatives) {
                    insertions.push_back({result.Hooks.Offset(PicaShaderHook::MainPrologue),
                                          std::string(kViewDerivativesPrologue)});
                    viewDerivativesInserted = true;
                    result.FragmentKey = SaltedVariantKey(result.FragmentKey, "OOT3D_NORMAL_GUIDE_VIEW_DERIVATIVES_V1");
                }
            }
            const std::string native = !result.Hooks.Has(PicaShaderSemantic::ViewVector)
                ? "oot3d_normal_guide_rotate_z(pica_normquat)"
                : prologueDerivatives ? std::string(kNativeViewNormalFromDerivatives)
                                      : "oot3d_normal_guide_surface(pica_normquat, pica_view)";
            if (typedNormal) {
                // The authored vertex normal, interpolated like the native
                // lighting's; the program's own quaternion or the geometric
                // normal remain for a draw whose normal is degenerate.
                declarations += R"glsl(
vec3 oot3d_normal_guide_typed(vec3 authored, vec3 native_normal) {
    float length2 = dot(authored, authored);
    return length2 > 1.0e-20 ? authored * inversesqrt(length2) : native_normal;
}
)glsl";
                epilogue += "    pica_normal_guide = vec4(oot3d_normal_guide_typed(pica_surface_normal, " + native +
                            ") * 0.5 + 0.5, 1.0);\n";
                result.FragmentKey = SaltedVariantKey(result.FragmentKey, "OOT3D_PICA_TYPED_NORMAL_GUIDE_V1");
                result.TypedNormalGuide = true;
            } else {
                epilogue += "    pica_normal_guide = vec4(" + native + " * 0.5 + 0.5, 1.0);\n";
            }
        }
        normalAvailable = true;
        return true;
    };

    const auto ensureMaterialGuide = [&](Decision::MaterialWriter writer) {
        if (materialAvailable) {
            return true;
        }
        if (!result.Hooks.Valid() ||
            HasOutputAtLocation(request.Source, 2U)) {
            return false;
        }
        decisions.MaterialAt = writer;
        declarations +=
            "layout(location=2) out vec4 pica_material_guide;\n";
        epilogue +=
            "    pica_material_guide = vec4(0.0); // OOT3D material guide baseline\n";
        materialAvailable = true;
        return true;
    };

    const auto apply = [&result](PicaShaderInstrumentationFeature feature) { result.AppliedFeatures |= feature; };

    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
            PicaShaderInstrumentationFeature::NativeFogGuide) && result.Hooks.Valid() &&
        !HasOutputAtLocation(request.Source, 5U)) {
        declarations += "layout(location=5) out vec4 pica_fog_guide;\n";
        decisions.Set(Decision::FogGuide);
        if (result.Hooks.Has(PicaShaderSemantic::NativeFogFactor)) decisions.Set(Decision::FogGuideFactor);
        epilogue += result.Hooks.Has(PicaShaderSemantic::NativeFogFactor)
            ? "    pica_fog_guide = vec4(fragment_uniforms.fog_color.rgb, fog_factor);\n"
            : "    pica_fog_guide = vec4(0.0, 0.0, 0.0, 1.0);\n";
        result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_NATIVE_FOG_GUIDE_V1");
        result.Outputs.WritesFogGuide = true;
        apply(PicaShaderInstrumentationFeature::NativeFogGuide);
    }

    if (HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::NormalGuide) &&
        ensureNormalGuide(Decision::NormalWriter::NormalGuide)) {
        result.FragmentKey = NormalGuideVariantKey(result.FragmentKey);
        apply(PicaShaderInstrumentationFeature::NormalGuide);
        const auto* relief = request.ReliefTexture < 3U ? result.Hooks.TextureSample(request.ReliefTexture) : nullptr;
        if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                                PicaShaderInstrumentationFeature::NormalGuideTextureRelief) &&
            !pbr && relief != nullptr && relief->Coordinate < 3U &&
            relief->Operation == PicaTextureCoordinateOperation::NativeVFlip &&
            request.Source.find("usampler2D pica_texture" + std::to_string(request.ReliefTexture)) ==
                std::string_view::npos &&
            std::isfinite(request.ReliefGain) && request.ReliefGain > 0.0F &&
            result.Hooks.Supports(PicaShaderHook::MainPrologue) &&
            result.Hooks.Has(PicaShaderSemantic::ViewVector)) {
            const std::string unit = std::to_string(request.ReliefTexture);
            const std::string coordinate = "pica_texcoord" + std::to_string(relief->Coordinate);
            std::ostringstream gain;
            gain.imbue(std::locale::classic());
            gain << std::fixed << std::setprecision(6) << request.ReliefGain;
            declarations += PicaTextureReliefDeclarations(gain.str());
            // Taps and derivatives come before the native alpha test can end
            // an invocation of the quad; the texture is sampled at the LOD
            // and wrap of the native sample, one texel of that LOD apart.
            insertions.push_back({result.Hooks.Offset(PicaShaderHook::MainPrologue),
                "\n    // OOT3D_AO_TEXTURE_RELIEF\n" + PicaTextureReliefPrologue(unit, coordinate)});
            // After every reader of the unbent guide (the outline geometry).
            deferredEpilogue += kPicaTextureReliefBend;
            decisions.Set(Decision::Relief);
            decisions.ReliefUnit = request.ReliefTexture;
            decisions.ReliefCoordinate = relief->Coordinate;
            decisions.ReliefGain = LiteralValue(request.ReliefGain);
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "OOT3D_AO_TEXTURE_RELIEF_V1");
            MixKey(result.FragmentKey, request.ReliefTexture);
            MixKey(result.FragmentKey, relief->Coordinate);
            MixKey(result.FragmentKey, std::bit_cast<uint32_t>(request.ReliefGain));
            apply(PicaShaderInstrumentationFeature::NormalGuideTextureRelief);
        }
    }

    // Geometry guide (location 6, uvec4): R/A native normal and depth for the
    // outlines, G/B the visible surface's face normal and kind. One output,
    // per-channel write masks select what each draw stores.
    const bool outlineGeometry =
        HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::OutlineGeometryGuide) &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        replacesDestinationRgb &&
        request.Draw.FragmentOperationMode == 0U && (request.Draw.ColorWriteMask & 7U) != 0U &&
        request.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
        !HasOutputAtLocation(request.Source, 6U) && ensureNormalGuide(Decision::NormalWriter::Outline);
    // The face normal follows the scene depth: every draw that writes depth
    // and the normal guide (the pipeline's normal-guide write rule).
    const bool faceNormal =
        HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::FaceNormalGuide) &&
        result.Hooks.Valid() && request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        request.Draw.FragmentOperationMode != 3U && (request.Draw.ColorWriteMask & 7U) != 0U &&
        request.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
        !HasOutputAtLocation(request.Source, 6U);
    if (outlineGeometry || faceNormal) {
        declarations += "layout(location=6) out uvec4 pica_geometry_guide;\n";
        declarations += Renderer3ds::PicaGeometryGuideShaderLibrary();
        epilogue += "    pica_geometry_guide = uvec4(0u, 0u, 0u, 0x3F800000u);\n";
    }
    const bool rasterInitializer = request.Draw.DepthCompare == ::Oot3d::Renderer::PicaCompareFunction::Always;
    if (outlineGeometry) {
        decisions.Set(Decision::Outline);
        if (rasterInitializer)
            decisions.Set(Decision::OutlineClear);
        const auto depth = AnalyzePicaFragmentOutputContract(request.Source).Depth;
        if (rasterInitializer) {
            // Raster color/depth initialization is not a geometric surface.
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_OUTLINE_CANVAS_CLEAR_V3");
        } else {
            epilogue += pbr
                ? "    pica_geometry_guide.r = pica_geometry_encode_normal(pbr_geometric_normal);\n"
                : "    pica_geometry_guide.r = pica_geometry_encode_normal(pica_normal_guide.rgb * 2.0 - 1.0);\n";
            epilogue += depth == ::Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative
                ? "    pica_geometry_guide.a = floatBitsToUint(gl_FragDepth);\n"
                : "    pica_geometry_guide.a = floatBitsToUint(gl_FragCoord.z);\n";
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_OUTLINE_GEOMETRY_GUIDE_V3");
        }
        result.Outputs.WritesOutlineGeometryGuide = true;
        apply(PicaShaderInstrumentationFeature::OutlineGeometryGuide);
    }
    if (faceNormal) {
        decisions.Set(Decision::FaceNormal);
        if (!rasterInitializer) {
            // Derivatives of the view vector are those of the view-space
            // position (negated); taken before the native alpha test when the
            // program has a main prologue. Without a view vector the kind
            // still marks the surface and the normal stays unknown.
            if (result.Hooks.Has(PicaShaderSemantic::ViewVector)) {
                const bool prologue = result.Hooks.Supports(PicaShaderHook::MainPrologue);
                if (prologue && !viewDerivativesInserted) {
                    insertions.push_back({result.Hooks.Offset(PicaShaderHook::MainPrologue),
                                          std::string(kViewDerivativesPrologue)});
                    viewDerivativesInserted = true;
                }
                epilogue += prologue
                    ? "    pica_geometry_guide.g = pica_geometry_encode_normal(\n"
                      "        pica_geometry_face_normal(pica_view, oot3d_view_dx, oot3d_view_dy));\n"
                    : "    pica_geometry_guide.g = pica_geometry_encode_normal(\n"
                      "        pica_geometry_face_normal(pica_view, dFdx(pica_view), dFdy(pica_view)));\n";
            }
            epilogue += "    pica_geometry_guide.b = 1u;\n";
        }
        result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_FACE_NORMAL_GUIDE_V1");
        result.Outputs.WritesFaceNormalGuide = true;
        apply(PicaShaderInstrumentationFeature::FaceNormalGuide);
    }

    // Global illumination surface guide: depth-writing scene surfaces publish
    // it (zero when their albedo is unknown, so a surface in front removes
    // GI); overlays that leave depth attenuate its alpha (scene-domain guide).
    // Each 16-bit RGB channel packs the sRGB albedo (high byte) and the sRGB
    // direct radiance that seeds the trace (low byte), so it is replaced, never
    // blended: a blended surface that writes depth (a fading NPC) owns the
    // depth and normal the trace reads, and its coverage is the share of the
    // native colour it replaces.
    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::DiffuseAlbedoGuide) &&
        result.Hooks.Valid() && request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        request.Draw.FragmentOperationMode == 0U &&
        (request.Draw.ColorWriteMask & 7U) != 0U &&
        request.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
        !HasOutputAtLocation(request.Source, 7U)) {
        std::string coverage = "1.0";
        if (!replacesDestinationRgb)
            coverage = blend.DestRgb == ::Oot3d::Renderer::NativeBlendFactor::One ? "0.0"
                                                                                  : "clamp(pica_color.a, 0.0, 1.0)";
        decisions.Set(Decision::AlbedoGuide);
        decisions.AlbedoCoverage = replacesDestinationRgb ? Decision::Coverage::One
            : blend.DestRgb == ::Oot3d::Renderer::NativeBlendFactor::One ? Decision::Coverage::Zero
            : Decision::Coverage::Alpha;
        // An isolated object neither receives nor bounces global illumination.
        const bool zeroAlbedo = request.Draw.DepthCompare == ::Oot3d::Renderer::PicaCompareFunction::Always ||
                                request.Draw.IsolatedLighting;
        decisions.Albedo =
            zeroAlbedo ? Decision::AlbedoPath::Zero
            : pbr ? Decision::AlbedoPath::Pbr
            : surfaceLighting ? Decision::AlbedoPath::Surface
            : Decision::AlbedoPath::Zero;
        declarations += "layout(location=7) out vec4 pica_diffuse_albedo_guide;\n";
        if (zeroAlbedo) {
            if (request.Draw.IsolatedLighting)
                result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_ISOLATED_ALBEDO_V1");
            epilogue += "    pica_diffuse_albedo_guide = vec4(0.0);\n";
        } else if (pbr) {
            epilogue += "    pica_diffuse_albedo_guide = vec4((floor(clamp(pbr_linear_to_srgb(clamp(pbr_albedo, 0.0, 1.0)), 0.0, 1.0) * 255.0 + 0.5) * 256.0 + floor(clamp(pbr_linear_to_srgb(pbr_gi_radiance), 0.0, 1.0) * 255.0 + 0.5)) / 65535.0, " + coverage + ");\n";
        } else if (surfaceLighting) {
            epilogue += "    pica_diffuse_albedo_guide = vec4((floor(surface_gi_albedo * 255.0 + 0.5) * 256.0 + floor(clamp(surface_gi_albedo * pica_surface_gi_direct(pica_surface_unit(pica_surface_normal)), 0.0, 1.0) * 255.0 + 0.5)) / 65535.0, " + coverage + ");\n";
        } else {
            epilogue += "    pica_diffuse_albedo_guide = vec4(0.0);\n";
        }
        result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_DIFFUSE_ALBEDO_GUIDE_V2_DIRECT");
        if (!replacesDestinationRgb)
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_DIFFUSE_ALBEDO_GUIDE_BLENDED_COVERAGE_V1");
        result.Outputs.WritesDiffuseAlbedoGuide = true;
        apply(PicaShaderInstrumentationFeature::DiffuseAlbedoGuide);
    }

    bool surfaceShadow = false;
    if (surfaceLighting && HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::DirectionalShadowLighting)) {
        const auto shadow =
            BuildPicaDirectionalShadowLightingInstrumentation(
                result.FragmentKey, true, result.Hooks);
        result.DirectionalShadowEligibility = shadow.Eligibility;
        if (shadow.Applied()) {
            declarations += shadow.Declarations;
            surfaceShadow = surfaceLighting;
            decisions.Set(Decision::DirectionalShadow);
            result.FragmentKey = shadow.FragmentKey;
            apply(PicaShaderInstrumentationFeature::
                      DirectionalShadowLighting);
        }
    }
    if (surfaceLighting) {
        declarations += surfaceShadow ? R"glsl(
vec3 pica_surface_shadow(int slot) {
    uint selected = uint(pica_vsm.receiver.w);
    if (slot < 0) return vec3(1.0);
    return (selected & (1u << uint(slot))) != 0u
        ? oot3d_directional_shadow_visibility(pica_surface_normal) : vec3(1.0);
}
)glsl" : "\nvec3 pica_surface_shadow(int slot) { return vec3(1.0); }\n";
        // Point lights read their cube map only when this sample has one.
        const bool pointShadow = HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures, PicaShaderInstrumentationFeature::PointShadowLighting);
        if (pointShadow) {
            decisions.Set(Decision::PointShadow);
            declarations += Renderer3ds::PointShadowReceiverShader();
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_POINT_SHADOW_V1");
            apply(PicaShaderInstrumentationFeature::PointShadowLighting);
        } else {
            declarations += "\nfloat pica_point_shadow(float slot, vec3 light_to_fragment, float radius) { return 1.0; }\n";
        }
    }

    if (!pbr && HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::Toon)) {
        if (!request.Draw.PerspectiveProjection && replacesDestinationRgb &&
            request.Draw.FragmentOperationMode == 0U &&
            request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
            request.Draw.DepthCompare == ::Oot3d::Renderer::PicaCompareFunction::Always) {
            // Orthographic color/depth replacement initializes a raster canvas;
            // its placeholder normal/view varyings are not a lighting surface.
            result.ToonEligibility = PicaToonEligibility::NoDepth;
        } else if (request.ToonStyle == nullptr) {
            result.ToonEligibility =
                PicaToonEligibility::UnsupportedShader;
        } else {
            const PicaToonDrawInfo toonDraw{
                request.Draw.CompositionDomain,
                request.Draw.DepthTestEnabled,
                request.Draw.DepthWriteEnabled,
                request.Draw.Blend.Enabled,
                request.Draw.ColorWriteMask,
            };
            auto toon = BuildPicaToonInstrumentation(
                result.FragmentKey, toonDraw, request.Toon,
                *request.ToonStyle, result.Hooks, request.ToonParametersUniform);
            result.ToonEligibility = toon.Eligibility;
            result.ToonMaterialPath = toon.MaterialPath;
            if (toon.Applied()) {
                decisions.Set(Decision::Toon);
                if (toon.MaterialPath) decisions.Set(Decision::ToonMaterialPath);
                if (result.Hooks.Has(PicaShaderSemantic::PrimaryColorConsumed))
                    decisions.Set(Decision::ToonVertexLighting);
                declarations += toon.Declarations;
                insertions.push_back({
                    result.Hooks.Offset(toon.InsertionHook),
                    std::move(toon.Body),
                });
                result.FragmentKey = toon.FragmentKey;
                apply(PicaShaderInstrumentationFeature::Toon);
            }
        }
    }

    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::AmbientOcclusionGuide)) {
        if (!result.Hooks.Valid() || (!ambientAvailable && HasOutputAtLocation(request.Source, 4U))) {
            result.AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::UnsupportedShader;
        } else {
            if (!ambientAvailable) {
                declarations += "layout(location=4) out vec4 pica_ambient_guide;\n";
                ambientAvailable = true;
            }
            const bool exact = result.Hooks.Has(PicaShaderSemantic::AmbientOcclusionResponse);
            decisions.Set(Decision::AmbientGuide);
            decisions.Ambient = request.Draw.IsolatedLighting ? Decision::AmbientPath::None
                : pbr ? Decision::AmbientPath::Pbr
                : exact ? Decision::AmbientPath::Exact : Decision::AmbientPath::Fallback;
            if (request.Draw.IsolatedLighting) {
                // An isolated object receives no ambient occlusion.
                epilogue += "\n    pica_ambient_guide = vec4(0.0, 0.0, 0.0, 1.0);\n";
                result.AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::AppliedExact;
                result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_ISOLATED_AMBIENT_V1");
            } else if (pbr) {
                epilogue += "\n    pica_ambient_guide = vec4(pbr_ao_response, 1.0);\n";
                result.AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::AppliedExact;
            } else if (exact) {
                epilogue += "\n    pica_ambient_guide = vec4("
                            "clamp(oot3d_ao_response_rgb, vec3(0.0), vec3(1.0)), 1.0);";
                result.AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::AppliedExact;
            } else {
                epilogue += "\n    pica_ambient_guide = vec4(1.0, 1.0, 1.0, 1.0);\n";
                result.AmbientGuideEligibility = PicaAmbientOcclusionGuideEligibility::AppliedFallback;
            }
            result.FragmentKey = AmbientVariantKey(result.FragmentKey);
            result.Outputs.WritesAmbientGuide = true;
            apply(PicaShaderInstrumentationFeature::AmbientOcclusionGuide);
        }
    }

    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::SceneDomainGuide)) {
        const auto& draw = request.Draw;
        const auto& features = request.SceneDomainFeatures;
        if (!features.AmbientOcclusion && !features.Outline && !features.GlobalIllumination) {
            result.SceneDomainEligibility = PicaSceneDomainGuideEligibility::Disabled;
        } else if (draw.CompositionDomain !=
                   ::Oot3d::Renderer::PicaCompositionDomain::Scene) {
            result.SceneDomainEligibility =
                PicaSceneDomainGuideEligibility::OutsideScene;
        } else if (draw.FragmentOperationMode != 0U || (draw.ColorWriteMask & 0x7U) == 0U) {
            result.SceneDomainEligibility = PicaSceneDomainGuideEligibility::NoRgbOutput;
        } else if (draw.DepthWriteEnabled && (!features.Outline || replacesDestinationRgb)) {
            result.SceneDomainEligibility = PicaSceneDomainGuideEligibility::SceneGeometry;
            if (features.Outline && !rigidMotionWillApply && !HasOutputAtLocation(request.Source, 3U)) {
                decisions.Set(Decision::SceneReset);
                declarations += "layout(location=3) out vec4 pica_transparent_depth_guide;\n";
                epilogue += "    pica_transparent_depth_guide = vec4(0.0);\n";
                result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_NATIVE_OCCLUSION_RESET_V2");
                apply(PicaShaderInstrumentationFeature::SceneDomainGuide);
            }
        } else if (!result.Hooks.Valid() || !result.Hooks.Has(PicaShaderSemantic::NativeColorOutput) ||
                   (features.Outline && !normalAvailable) ||
                   (features.AmbientOcclusion && !ambientAvailable && HasOutputAtLocation(request.Source, 4U)) ||
                   (features.GlobalIllumination && HasOutputAtLocation(request.Source, 7U))) {
            result.SceneDomainEligibility = PicaSceneDomainGuideEligibility::UnsupportedShader;
        } else {
            if (features.AmbientOcclusion && !ambientAvailable) {
                declarations += "layout(location=4) out vec4 pica_ambient_guide;\n";
                ambientAvailable = true;
            }
            const bool transparentDepthAvailable =
                features.Outline && draw.Blend.Enabled && draw.DepthTestEnabled &&
                draw.DepthCompare != ::Oot3d::Renderer::PicaCompareFunction::Always &&
                !HasOutputAtLocation(request.Source, 3U);
            decisions.Set(Decision::SceneOverlay);
            if (transparentDepthAvailable) {
                decisions.Set(Decision::SceneTransparentDepth);
                if (rigidMotionWillApply) decisions.Set(Decision::SceneTransparentRigid);
                const char* outputName = "pica_rigid_motion_guide";
                if (!rigidMotionWillApply) {
                    outputName = "pica_transparent_depth_guide";
                    declarations +=
                        "layout(location=3) out vec4 "
                        "pica_transparent_depth_guide;\n";
                    deferredEpilogue +=
                        "    pica_transparent_depth_guide.rgb = vec3(0.0);\n";
                }
                deferredEpilogue += "    ";
                deferredEpilogue += outputName;
                const auto depth = AnalyzePicaFragmentOutputContract(request.Source).Depth;
                deferredEpilogue += ".a = pica_color.a > 0.0039215686 ? clamp(1.0 - ";
                deferredEpilogue += depth == ::Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative
                    ? "gl_FragDepth" : "gl_FragCoord.z";
                deferredEpilogue += ", 0.0000152588, 1.0) : 0.0;\n";
            }
            const char* alpha = draw.Blend.Enabled ? "pica_color.a" : "0.0";
            if (draw.Blend.Enabled) decisions.Set(Decision::SceneBlended);
            if (features.Outline) decisions.Set(Decision::SceneOutline);
            if (features.AmbientOcclusion) decisions.Set(Decision::SceneAmbient);
            if (draw.Blend.Enabled) {
                epilogue += "    // OOT3D_SCENE_DOMAIN_BLENDED_OVERLAY\n";
            }
            if (features.Outline) {
                epilogue += "    pica_normal_guide.a = ";
                epilogue += alpha;
                epilogue += "; // OOT3D_SCENE_DOMAIN_NORMAL_OVERLAY\n";
            }
            if (features.AmbientOcclusion) {
                epilogue += "    pica_ambient_guide = vec4(1.0, 1.0, 1.0, ";
                epilogue += alpha;
                epilogue += "); // OOT3D_SCENE_DOMAIN_AMBIENT_OVERLAY\n";
            }
            // GI is light on the surface below: an overlay transmits only its
            // native destination factor of it (the alpha-only coverage blend).
            // A depth-writing surface already published its own guide.
            const bool albedoOverlay = features.GlobalIllumination && !result.Outputs.WritesDiffuseAlbedoGuide;
            if (albedoOverlay) {
                decisions.Set(Decision::SceneAlbedo);
                declarations += "layout(location=7) out vec4 pica_diffuse_albedo_guide;\n";
                epilogue += "    pica_diffuse_albedo_guide = vec4(0.0, 0.0, 0.0, ";
                epilogue += alpha;
                epilogue += "); // OOT3D_SCENE_DOMAIN_ALBEDO_OVERLAY\n";
            }
            result.FragmentKey = SceneDomainVariantKey(result.FragmentKey, features, draw.Blend.Enabled);
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_NATIVE_OCCLUSION_DEPTH_V2");
            result.SceneDomainEligibility = draw.Blend.Enabled ? PicaSceneDomainGuideEligibility::AppliedBlendedOverlay
                                                              : PicaSceneDomainGuideEligibility::AppliedOpaqueOverlay;
            result.Outputs.SceneDomainBlendedOverlay =
                draw.Blend.Enabled;
            result.Outputs.SceneDomainNormalOverlay = features.Outline;
            result.Outputs.SceneDomainAmbientOverlay =
                features.AmbientOcclusion;
            result.Outputs.SceneDomainTransparentDepthOverlay =
                transparentDepthAvailable;
            result.Outputs.SceneDomainAlbedoOverlay = albedoOverlay;
            result.Outputs.WritesAmbientGuide |=
                features.AmbientOcclusion;
            apply(PicaShaderInstrumentationFeature::SceneDomainGuide);
        }
    }

    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::ReflectionMaterialGuide)) {
        const PicaReflectionMaterialDrawInfo draw{
            request.Draw.CompositionDomain, request.Draw.FragmentOperationMode,
            request.Draw.DepthTestEnabled, request.Draw.DepthWriteEnabled, request.Draw.ColorWriteMask,
        };
        result.ReflectionEligibility =
            ClassifyPicaReflectionMaterialDraw(
                result.Hooks, draw, true,
                request.ReflectionProfile);
        if (pbr && draw.DepthTestEnabled && draw.DepthWriteEnabled &&
            draw.FragmentOperationMode == 0U && (draw.ColorWriteMask & 7U) != 0U)
            result.ReflectionEligibility = PicaReflectionMaterialEligibility::Eligible;
        const bool reflectionApplied =
            result.ReflectionEligibility == PicaReflectionMaterialEligibility::Eligible ||
            result.ReflectionEligibility == PicaReflectionMaterialEligibility::ExplicitTextureProfile;
        if (reflectionApplied && ensureMaterialGuide(Decision::MaterialWriter::Reflection)) {
            declarations += Renderer3ds::PicaMaterialGuideShaderLibrary;
            decisions.Set(Decision::Reflection);
            if (pbr) {
                decisions.Set(Decision::ReflectionPbr);
                epilogue += "    pica_material_guide = vec4(pbr_reflectivity, pbr_roughness, "
                    "pica_encode_reflection_weight(pbr_material.specular_reflection_weight.y), 0.0);\n";
                apply(PicaShaderInstrumentationFeature::ReflectionMaterialGuide);
            } else if (request.ReflectionProfile != nullptr) {
                const auto profile =
                    SanitizeProfile(*request.ReflectionProfile);
                decisions.Set(Decision::ReflectionExplicit);
                decisions.ReflectionReflectivity = LiteralValue(profile.Reflectivity);
                decisions.ReflectionRoughness = LiteralValue(profile.Roughness);
                epilogue += "    ";
                epilogue += BuildExplicitMaterialGuide(profile);
                epilogue += "\n";
                result.FragmentKey ^= 0x7265666c74787402ULL;
                MixKey(result.FragmentKey,
                       std::bit_cast<uint32_t>(profile.Reflectivity));
                MixKey(result.FragmentKey,
                       std::bit_cast<uint32_t>(profile.Roughness));
                apply(PicaShaderInstrumentationFeature::
                          ReflectionMaterialGuide);
            } else {
                epilogue += BuildCalibratedMaterialGuide();
                result.FragmentKey ^= 0x7265666c6d617402ULL;
                apply(PicaShaderInstrumentationFeature::
                          ReflectionMaterialGuide);
            }
        } else if (reflectionApplied) {
            result.ReflectionEligibility =
                PicaReflectionMaterialEligibility::UnsupportedShader;
        }
    }

    if (HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::ReactiveMask)) {
        const auto reactive = ResolvePicaReactiveMaskPlan({
            request.Draw.Blend,
            request.Draw.CompositionDomain,
            request.Draw.FragmentOperationMode,
            request.Draw.DepthTestEnabled,
            request.Draw.ColorWriteMask,
        });
        const bool sourceCoverageAvailable =
            reactive.Coverage == PicaReactiveCoverage::Full ||
            result.Hooks.Has(PicaShaderSemantic::NativeColorOutput);
        if (reactive.Reactive() && sourceCoverageAvailable &&
            ensureMaterialGuide(Decision::MaterialWriter::Reactive)) {
            decisions.Set(Decision::Reactive);
            decisions.ReactiveKind = static_cast<Decision::ReactiveCoverage>(reactive.Coverage);
            epilogue += BuildPicaReactiveMaskAssignment(reactive);
            result.FragmentKey = ResolvePicaReactiveFragmentKey(
                result.FragmentKey, reactive.Coverage);
            result.ReactiveCoverage = reactive.Coverage;
            result.Reactive = true;
            apply(PicaShaderInstrumentationFeature::ReactiveMask);
        }
    }

    // Every opaque guide-writing draw has the material attachment in its
    // write mask (pica_nri_pipeline_state). Guides are cleared only with the
    // native colour, so a draw that leaves location 2 unwritten keeps the
    // value of an earlier presented sample: an interpolated sample showed the
    // previous sample's reflectivity and reactivity on the ground Link had
    // just left. Such draws write the neutral baseline.
    if ((HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                             PicaShaderInstrumentationFeature::ReflectionMaterialGuide) ||
         HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                             PicaShaderInstrumentationFeature::ReactiveMask)) &&
        !materialAvailable && request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        (request.Draw.ColorWriteMask & 7U) != 0U && ensureMaterialGuide(Decision::MaterialWriter::Baseline)) {
        result.FragmentKey ^= 0x6d61746c62617365ULL;
        apply(PicaShaderInstrumentationFeature::MaterialGuideBaseline);
    }

    if (rigidMotionWillApply) {
        decisions.Set(Decision::RigidMotion);
        declarations +=
            "layout(location=3) out vec4 pica_rigid_motion_guide;\n"
            "layout(location=7) in vec4 oot3d_current_clip;\n"
            "layout(location=8) in vec4 oot3d_previous_clip;\n"
            "layout(push_constant) uniform Oot3dDrawState { vec4 rigid_motion; vec4 jitter; } oot3d_draw;\n";
        epilogue += "    vec4 oot3d_motion = oot3d_draw.rigid_motion;\n"
                    "    if (oot3d_draw.rigid_motion.a > 0.5 &&\n"
                    "        oot3d_current_clip.w > 1.0e-7 && oot3d_previous_clip.w > 1.0e-7) {\n"
                    "        vec2 oot3d_current_uv = oot3d_current_clip.xy / oot3d_current_clip.w * 0.5 + 0.5;\n"
                    "        vec2 oot3d_previous_uv = oot3d_previous_clip.xy / oot3d_previous_clip.w * 0.5 + 0.5;\n"
                    "        oot3d_motion = vec4(oot3d_previous_uv - oot3d_current_uv, 1.0, 1.0);\n"
                    "    }\n"
                    "    pica_rigid_motion_guide = vec4(oot3d_motion.xy, oot3d_motion.z, 0.0);\n";
        result.FragmentKey ^= 0x52494749444d4f54ULL;
        result.RigidMotionApplied = true;
        apply(PicaShaderInstrumentationFeature::RigidMotionGuide);
    }

    epilogue += deferredEpilogue;
    if (surfaceLighting && result.Hooks.Has(PicaShaderSemantic::NativeColorOutput)) {
        decisions.Set(Decision::EnvironmentMix);
        epilogue += "\n    // Water keeps its native fog (its native colour is its appearance;\n"
                    "    // far water meets the native horizon).\n"
                    "    if (pica_surface.environment_parameters.w != 0.0 && !pica_surface_water_fragment)\n"
                    "        pica_color.rgb = mix(surface_before_fog, pica_color.rgb, pica_surface.environment_parameters.z);\n";
    }
    if (pbr && result.Hooks.Has(PicaShaderSemantic::NativeColorOutput))
        epilogue += pbr->DiagnosticOutput;
    if (surfaceLighting && !pbr && materialAvailable)
        epilogue += Renderer3ds::PicaSurfaceLightingWaterMarker();

    if (result.Applied()) {
        result.Source = Compose(
            request.Source, result.Hooks, declarations, epilogue,
            insertions);
        for (uint32_t location = 1U; location < 8U; ++location)
            if (HasOutputAtLocation(result.Source, location))
                decisions.Outputs |= static_cast<uint8_t>(1U << location);
        if (result.Source.empty()) {
            result.Decisions = {};
            result.AppliedFeatures = PicaShaderInstrumentationFeature::None;
            result.FragmentKey = request.FragmentKey;
            result.ReactiveCoverage = PicaReactiveCoverage::None;
            result.Reactive = false;
            result.RigidMotionApplied = false;
            result.TypedNormalGuide = false;
        }
    }
    return result;
}

} // namespace Fast::Oot3d
