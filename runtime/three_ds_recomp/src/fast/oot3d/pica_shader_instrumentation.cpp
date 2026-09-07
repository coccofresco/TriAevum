#include "fast/oot3d/pica_shader_instrumentation.h"

#include <algorithm>
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
        finiteClamp(profile.MaterialClass, 0.5F, 1.0F, 1.0F),
    };
}

std::string BuildExplicitMaterialGuide(const ReflectionMaterialParameters& profile) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << "pica_material_guide = vec4(" << profile.Reflectivity << ", "
           << profile.Roughness << ", " << profile.MaterialClass << ", 0.0); // OOT3D explicit texture material";
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
           "0.75, 0.0); // OOT3D calibrated specular material\n";
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

    const auto ensureNormalGuide = [&]() {
        if (normalAvailable) {
            return true;
        }
        const bool materialNormal =
            result.Hooks.Has(PicaShaderSemantic::MaterialNormal);
        const bool normalQuaternion =
            result.Hooks.Has(PicaShaderSemantic::NormalQuaternion);
        if (!result.Hooks.Valid() ||
            (!materialNormal && !normalQuaternion) ||
            HasOutputAtLocation(request.Source, 1U)) {
            return false;
        }
        declarations +=
            "layout(location=1) out vec4 pica_normal_guide;\n";
        if (materialNormal) {
            epilogue +=
                "    pica_normal_guide = vec4(normal * 0.5 + 0.5, 1.0);\n";
        } else {
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
            if (result.Hooks.Has(PicaShaderSemantic::ViewVector)) {
                declarations += R"glsl(
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
            }
            epilogue +=
                result.Hooks.Has(PicaShaderSemantic::ViewVector)
                    ? "    pica_normal_guide = vec4(oot3d_normal_guide_surface(pica_normquat, pica_view) * 0.5 + 0.5, 1.0);\n"
                    : "    pica_normal_guide = vec4(oot3d_normal_guide_rotate_z(pica_normquat) * 0.5 + 0.5, 1.0);\n";
        }
        normalAvailable = true;
        return true;
    };

    const auto ensureMaterialGuide = [&]() {
        if (materialAvailable) {
            return true;
        }
        if (!result.Hooks.Valid() ||
            HasOutputAtLocation(request.Source, 2U)) {
            return false;
        }
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
        ensureNormalGuide()) {
        result.FragmentKey = NormalGuideVariantKey(result.FragmentKey);
        apply(PicaShaderInstrumentationFeature::NormalGuide);
    }

    if (HasPicaShaderInstrumentationFeature(request.RequestedFeatures,
                                            PicaShaderInstrumentationFeature::OutlineGeometryGuide) &&
        request.Draw.DepthTestEnabled && request.Draw.DepthWriteEnabled &&
        replacesDestinationRgb &&
        request.Draw.FragmentOperationMode == 0U && (request.Draw.ColorWriteMask & 7U) != 0U &&
        request.Draw.CompositionDomain == ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
        !HasOutputAtLocation(request.Source, 6U) && ensureNormalGuide()) {
        declarations += "layout(location=6) out vec4 pica_outline_geometry_guide;\n";
        const auto depth = AnalyzePicaFragmentOutputContract(request.Source).Depth;
        if (request.Draw.DepthCompare == ::Oot3d::Renderer::PicaCompareFunction::Always) {
            // Raster color/depth initialization is not a geometric surface.
            epilogue += "    pica_outline_geometry_guide = vec4(0.0, 0.0, 0.0, 1.0);\n";
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_OUTLINE_CANVAS_CLEAR_V2");
        } else {
            epilogue += "    pica_outline_geometry_guide = vec4(pica_normal_guide.rgb * 2.0 - 1.0, ";
            epilogue += depth == ::Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative ? "gl_FragDepth);\n"
                                                                                       : "gl_FragCoord.z);\n";
            result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_OUTLINE_GEOMETRY_GUIDE_V2");
        }
        result.Outputs.WritesOutlineGeometryGuide = true;
        apply(PicaShaderInstrumentationFeature::OutlineGeometryGuide);
    }

    if (HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::DirectionalShadowLighting)) {
        const auto shadow =
            BuildPicaDirectionalShadowLightingInstrumentation(
                result.FragmentKey, true, result.Hooks);
        result.DirectionalShadowEligibility = shadow.Eligibility;
        if (shadow.Applied()) {
            declarations += shadow.Declarations;
            insertions.push_back({
                result.Hooks.Offset(shadow.InsertionHook), shadow.Body});
            result.FragmentKey = shadow.FragmentKey;
            apply(PicaShaderInstrumentationFeature::
                      DirectionalShadowLighting);
        }
    }

    if (HasPicaShaderInstrumentationFeature(
            request.RequestedFeatures,
            PicaShaderInstrumentationFeature::Toon)) {
        if (request.ToonStyle == nullptr) {
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
                *request.ToonStyle, result.Hooks);
            result.ToonEligibility = toon.Eligibility;
            result.ToonMaterialPath = toon.MaterialPath;
            if (toon.Applied()) {
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
            if (exact) {
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
        if (!features.AmbientOcclusion && !features.Outline) {
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
                declarations += "layout(location=3) out vec4 pica_transparent_depth_guide;\n";
                epilogue += "    pica_transparent_depth_guide = vec4(0.0);\n";
                result.FragmentKey = SaltedVariantKey(result.FragmentKey, "PICA_NATIVE_OCCLUSION_RESET_V2");
                apply(PicaShaderInstrumentationFeature::SceneDomainGuide);
            }
        } else if (!result.Hooks.Valid() || !result.Hooks.Has(PicaShaderSemantic::NativeColorOutput) ||
                   (features.Outline && !normalAvailable) ||
                   (features.AmbientOcclusion && !ambientAvailable && HasOutputAtLocation(request.Source, 4U))) {
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
            if (transparentDepthAvailable) {
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
        const bool reflectionApplied =
            result.ReflectionEligibility == PicaReflectionMaterialEligibility::Eligible ||
            result.ReflectionEligibility == PicaReflectionMaterialEligibility::ExplicitTextureProfile;
        if (reflectionApplied && ensureMaterialGuide()) {
            if (request.ReflectionProfile != nullptr) {
                const auto profile =
                    SanitizeProfile(*request.ReflectionProfile);
                epilogue += "    ";
                epilogue += BuildExplicitMaterialGuide(profile);
                epilogue += "\n";
                result.FragmentKey ^= 0x7265666c74787401ULL;
                MixKey(result.FragmentKey,
                       std::bit_cast<uint32_t>(profile.Reflectivity));
                MixKey(result.FragmentKey,
                       std::bit_cast<uint32_t>(profile.Roughness));
                MixKey(result.FragmentKey,
                       std::bit_cast<uint32_t>(profile.MaterialClass));
                apply(PicaShaderInstrumentationFeature::
                          ReflectionMaterialGuide);
            } else {
                epilogue += BuildCalibratedMaterialGuide();
                result.FragmentKey ^= 0x7265666c6d617401ULL;
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
            ensureMaterialGuide()) {
            epilogue += BuildPicaReactiveMaskAssignment(reactive);
            result.FragmentKey = ResolvePicaReactiveFragmentKey(
                result.FragmentKey, reactive.Coverage);
            result.ReactiveCoverage = reactive.Coverage;
            result.Reactive = true;
            apply(PicaShaderInstrumentationFeature::ReactiveMask);
        }
    }

    if (rigidMotionWillApply) {
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

    if (result.Applied()) {
        result.Source = Compose(
            request.Source, result.Hooks, declarations, epilogue,
            insertions);
        if (result.Source.empty()) {
            result.AppliedFeatures = PicaShaderInstrumentationFeature::None;
            result.FragmentKey = request.FragmentKey;
            result.ReactiveCoverage = PicaReactiveCoverage::None;
            result.Reactive = false;
            result.RigidMotionApplied = false;
        }
    }
    return result;
}

} // namespace Fast::Oot3d
