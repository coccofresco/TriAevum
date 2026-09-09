#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_proctex.h"
#include "fast/oot3d/pica_fragment_lighting.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::array<uint16_t, 6> kTevStageRegisters{
    0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U};

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string DescribeUnsupportedTev(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state) {
    for (size_t stage = 0; stage < kTevStageRegisters.size(); ++stage) {
        const uint16_t base = kTevStageRegisters[stage];
        const uint32_t sources = packet.Registers[base];
        const uint32_t modifiers = packet.Registers[base + 1U];
        const uint32_t operations = packet.Registers[base + 2U];
        for (size_t input = 0; input < 3U; ++input) {
            const std::array<std::pair<const char*, uint8_t>, 2> inputs{{
                {"color", static_cast<uint8_t>(
                              (sources >> (input * 4U)) & 0xFU)},
                {"alpha", static_cast<uint8_t>(
                              (sources >> (16U + input * 4U)) & 0xFU)},
            }};
            for (const auto& [channel, source] : inputs) {
                const bool knownSource = source <= 0x6U || source == 0xDU ||
                                         source == 0xEU || source == 0xFU;
                if (!knownSource ||
                    (source == 0x3U && state.Textures[0].Enabled &&
                     state.Textures[0].Type != 0U &&
                     state.Textures[0].Type != 2U &&
                     state.Textures[0].Type != 3U &&
                     state.Textures[0].Type != 5U)) {
                    std::ostringstream detail;
                    detail << "PICA TEV stage " << stage << ' ' << channel
                           << " input " << input << " source 0x" << std::hex
                           << static_cast<uint32_t>(source) << std::dec;
                    if (source == 0x3U) {
                        detail << " uses unsupported texture0 type "
                               << static_cast<uint32_t>(state.Textures[0].Type);
                    }
                    return detail.str();
                }
            }
            const uint8_t colorModifier = static_cast<uint8_t>(
                (modifiers >> (input * 4U)) & 0xFU);
            switch (colorModifier) {
            case 0x0U:
            case 0x1U:
            case 0x2U:
            case 0x3U:
            case 0x4U:
            case 0x5U:
            case 0x8U:
            case 0x9U:
            case 0xCU:
            case 0xDU:
                break;
            default: {
                std::ostringstream detail;
                detail << "PICA TEV stage " << stage << " color input "
                       << input << " modifier 0x" << std::hex
                       << static_cast<uint32_t>(colorModifier);
                return detail.str();
            }
            }
        }
        const uint8_t colorOperation =
            static_cast<uint8_t>(operations & 0xFU);
        const uint8_t alphaOperation =
            static_cast<uint8_t>((operations >> 16U) & 0xFU);
        if (colorOperation > 9U) {
            std::ostringstream detail;
            detail << "PICA TEV stage " << stage << " color operation 0x"
                   << std::hex << static_cast<uint32_t>(colorOperation);
            return detail.str();
        }
        if (colorOperation != 7U && alphaOperation > 5U &&
            alphaOperation != 8U && alphaOperation != 9U) {
            std::ostringstream detail;
            detail << "PICA TEV stage " << stage << " alpha operation 0x"
                   << std::hex << static_cast<uint32_t>(alphaOperation);
            return detail.str();
        }
    }
    return "PICA TEV stage uses an unsupported source or operation";
}

uint64_t HashWord(uint64_t hash, uint32_t word) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (size_t byte = 0; byte < sizeof(word); ++byte) {
        hash ^= static_cast<uint8_t>(word >> (byte * 8U));
        hash *= kFnvPrime;
    }
    return hash;
}

std::array<float, 4> DecodeColor(uint32_t value) {
    return {
        static_cast<float>(value & 0xFFU) / 255.0F,
        static_cast<float>((value >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>((value >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((value >> 24U) & 0xFFU) / 255.0F,
    };
}

float DecodeFloat16(uint32_t raw) {
    constexpr uint32_t kExponentMask = 0x1FU;
    constexpr uint32_t kMantissaMask = 0x3FFU;
    const uint32_t exponent = (raw >> 10U) & kExponentMask;
    const uint32_t mantissa = raw & kMantissaMask;
    const bool negative = (raw & 0x8000U) != 0U;
    float value = 0.0F;
    if (exponent == kExponentMask) {
        value = mantissa == 0U
                    ? std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::quiet_NaN();
    } else if (exponent == 0U) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else {
        value = std::ldexp(
            1.0F + static_cast<float>(mantissa) / 1024.0F,
            static_cast<int>(exponent) - 15);
    }
    return negative ? -value : value;
}

std::string Shadow2dHelpers(const Oot3dPicaDecodedDrawState& state) {
    if (!state.Textures[0].Enabled || state.Textures[0].Type != 2U) {
        return {};
    }
    return R"glsl(
uvec2 pica_decode_shadow2d(uint pixel) {
    return uvec2(pixel >> 8u, pixel & 0xffu);
}
float pica_compare_shadow2d(uint pixel, uint z) {
    uvec2 value = pica_decode_shadow2d(pixel);
    return value.x > z ? float(value.y) * (1.0 / 255.0) : 0.0;
}
float pica_sample_shadow2d_texel(ivec2 coordinate, uint z) {
    ivec2 size = textureSize(pica_texture0, 0);
    if (any(lessThan(coordinate, ivec2(0))) ||
        any(greaterThanEqual(coordinate, size))) {
        return 1.0;
    }
    return pica_compare_shadow2d(
        texelFetch(pica_texture0, coordinate, 0).x, z);
}
vec4 pica_sample_shadow2d(vec2 uv, float w) {
    if (fragment_uniforms.shadow_orthographic == 0) {
        uv /= w;
    }
    uint z = uint(max(
        0, int(min(abs(w), 1.0) * 16777215.0) -
               fragment_uniforms.shadow_texture_bias));
    vec2 coordinate = vec2(textureSize(pica_texture0, 0)) * uv - vec2(0.5);
    ivec2 base = ivec2(floor(coordinate));
    vec2 factor = fract(coordinate);
    vec4 samples = vec4(
        pica_sample_shadow2d_texel(base, z),
        pica_sample_shadow2d_texel(base + ivec2(1, 0), z),
        pica_sample_shadow2d_texel(base + ivec2(0, 1), z),
        pica_sample_shadow2d_texel(base + ivec2(1, 1), z));
    vec2 vertical = mix(samples.xy, samples.zw, factor.yy);
    return vec4(mix(vertical.x, vertical.y, factor.x));
}
)glsl";
}

std::string ShadowWriteHelpers(const Oot3dPicaDecodedDrawState& state) {
    if (state.OutputMerger.FragmentOperationMode != 3U) {
        return {};
    }
    return R"glsl(
uint pica_encode_shadow2d(uvec2 pixel) {
    return (pixel.x << 8u) | pixel.y;
}
uint pica_update_shadow2d(uint pixel, uint depth24, uint penumbra8) {
    uvec2 reference = uvec2(pixel >> 8u, pixel & 0xffu);
    if (depth24 < reference.x) {
        if (penumbra8 == 0u) {
            reference.x = depth24;
        } else {
            float divisor = fragment_uniforms.shadow_bias_constant +
                fragment_uniforms.shadow_bias_linear *
                    float(depth24) / float(reference.x);
            penumbra8 = uint(float(penumbra8) / divisor);
            reference.y = min(penumbra8, reference.y);
        }
    }
    return pica_encode_shadow2d(reference);
}
)glsl";
}

std::string TextureSampleExpression(
    size_t texture, const Oot3dPicaDecodedDrawState& state,
    bool& supported) {
    if (texture >= state.Textures.size()) {
        supported = false;
        return "vec4(0.0)";
    }
    if (!state.Textures[texture].Enabled) {
        return "vec4(0.0)";
    }
    const size_t coordinate =
        texture == 2U && state.Texture2UsesCoordinate1 ? 1U : texture;
    if (texture == 0U && state.Textures[texture].Type == 3U) {
        return "textureProj(pica_texture0, vec3(pica_texcoord0.x, "
               "pica_texcoord0_w - pica_texcoord0.y, "
               "pica_texcoord0_w))";
    }
    if (texture == 0U && state.Textures[texture].Type == 2U) {
        return "pica_sample_shadow2d(pica_texcoord0, pica_texcoord0_w)";
    }
    if (texture == 0U && state.Textures[texture].Type == 5U) {
        return "vec4(0.0)";
    }
    if (texture == 0U && state.Textures[texture].Type != 0U) {
        supported = false;
        return "vec4(0.0)";
    }
    const std::string texcoord =
        "pica_texcoord" + std::to_string(coordinate);
    return "pica_sample_texture" + std::to_string(texture) +
           "(vec2(" + texcoord + ".x, 1.0 - " + texcoord + ".y))";
}

std::string SourceExpression(uint8_t source, size_t stage,
                             const Oot3dPicaDecodedDrawState& state,
                             bool procTexEnabled, bool& supported) {
    switch (source) {
    case 0x0:
        return "rounded_primary_color";
    case 0x1:
        return "primary_fragment_color";
    case 0x2:
        return "secondary_fragment_color";
    case 0x3:
    case 0x4:
    case 0x5: {
        const size_t texture = source - 0x3U;
        return TextureSampleExpression(texture, state, supported);
    }
    case 0x6:
        return procTexEnabled ? "pica_sample_proctex()" : "vec4(0.0)";
    case 0xD:
        return "combiner_buffer";
    case 0xE:
        return "fragment_uniforms.tev_constants[" +
               std::to_string(stage) + "]";
    case 0xF:
        return "combiner_output";
    default:
        supported = false;
        return "vec4(0.0)";
    }
}

std::string ColorModifier(const std::string& source, uint8_t modifier,
                          bool& supported) {
    switch (modifier) {
    case 0x0:
        return source + ".rgb";
    case 0x1:
        return "vec3(1.0) - " + source + ".rgb";
    case 0x2:
        return source + ".aaa";
    case 0x3:
        return "vec3(1.0) - " + source + ".aaa";
    case 0x4:
        return source + ".rrr";
    case 0x5:
        return "vec3(1.0) - " + source + ".rrr";
    case 0x8:
        return source + ".ggg";
    case 0x9:
        return "vec3(1.0) - " + source + ".ggg";
    case 0xC:
        return source + ".bbb";
    case 0xD:
        return "vec3(1.0) - " + source + ".bbb";
    default:
        supported = false;
        return "vec3(0.0)";
    }
}

std::string AlphaModifier(const std::string& source, uint8_t modifier,
                          bool& supported) {
    switch (modifier) {
    case 0:
        return source + ".a";
    case 1:
        return "1.0 - " + source + ".a";
    case 2:
        return source + ".r";
    case 3:
        return "1.0 - " + source + ".r";
    case 4:
        return source + ".g";
    case 5:
        return "1.0 - " + source + ".g";
    case 6:
        return source + ".b";
    case 7:
        return "1.0 - " + source + ".b";
    default:
        supported = false;
        return "0.0";
    }
}

std::string ColorOperation(uint8_t operation, const std::string& a,
                           const std::string& b, const std::string& c,
                           bool& supported) {
    switch (operation) {
    case 0:
        return a;
    case 1:
        return a + " * " + b;
    case 2:
        return a + " + " + b;
    case 3:
        return a + " + " + b + " - vec3(0.5)";
    case 4:
        return "mix(" + b + ", " + a + ", " + c + ")";
    case 5:
        return a + " - " + b;
    case 6:
    case 7:
        return "vec3(dot(" + a + " - vec3(0.5), " + b +
               " - vec3(0.5)) * 4.0)";
    case 8:
        return "fma(" + a + ", " + b + ", " + c + ")";
    case 9:
        return "min(" + a + " + " + b + ", vec3(1.0)) * " + c;
    default:
        supported = false;
        return "vec3(0.0)";
    }
}

std::string AlphaOperation(uint8_t operation, const std::string& a,
                           const std::string& b, const std::string& c,
                           bool& supported) {
    switch (operation) {
    case 0:
        return a;
    case 1:
        return a + " * " + b;
    case 2:
        return a + " + " + b;
    case 3:
        return a + " + " + b + " - 0.5";
    case 4:
        return "mix(" + b + ", " + a + ", " + c + ")";
    case 5:
        return a + " - " + b;
    case 8:
        return "fma(" + a + ", " + b + ", " + c + ")";
    case 9:
        return "min(" + a + " + " + b + ", 1.0) * " + c;
    default:
        supported = false;
        return "0.0";
    }
}

std::string AlphaDiscardCondition(uint8_t function) {
    switch (function) {
    case 0:
        return "true";
    case 1:
        return "false";
    case 2:
        return "alpha_byte != fragment_uniforms.alpha_reference";
    case 3:
        return "alpha_byte == fragment_uniforms.alpha_reference";
    case 4:
        return "alpha_byte >= fragment_uniforms.alpha_reference";
    case 5:
        return "alpha_byte > fragment_uniforms.alpha_reference";
    case 6:
        return "alpha_byte <= fragment_uniforms.alpha_reference";
    case 7:
        return "alpha_byte < fragment_uniforms.alpha_reference";
    default:
        return "false";
    }
}

uint8_t TevOperationInputMask(uint8_t operation) {
    switch (operation) {
    case 0U:
        return 0x1U;
    case 1U:
    case 2U:
    case 3U:
    case 5U:
    case 6U:
    case 7U:
        return 0x3U;
    case 4U:
    case 8U:
    case 9U:
        return 0x7U;
    default:
        return 0U;
    }
}

uint8_t SampledTextureMask(
    const std::array<uint8_t, 3>& sources, uint8_t inputMask,
    const Oot3dPicaDecodedDrawState& state) {
    uint8_t result = 0U;
    for (size_t input = 0U; input < sources.size(); ++input) {
        if ((inputMask & (1U << input)) == 0U || sources[input] < 0x3U ||
            sources[input] > 0x5U) {
            continue;
        }
        const size_t texture = sources[input] - 0x3U;
        if (!state.Textures[texture].Enabled ||
            (texture == 0U && state.Textures[texture].Type == 5U)) {
            continue;
        }
        result |= static_cast<uint8_t>(1U << texture);
    }
    return result;
}

bool InputMaskReferencesSource(
    const std::array<uint8_t, 3>& sources, uint8_t inputMask,
    uint8_t source) {
    for (size_t input = 0U; input < sources.size(); ++input) {
        if ((inputMask & (1U << input)) != 0U &&
            sources[input] == source) {
            return true;
        }
    }
    return false;
}

size_t StreamOffset(std::ostringstream& stream) {
    return static_cast<size_t>(
        static_cast<std::streamoff>(stream.tellp()));
}

} // namespace

uint64_t ComputeOot3dPicaFragmentShaderStateKey(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
    uint64_t key = HashWord(kFnvOffset, packet.Registers[0x080U]);
    key = HashWord(key, packet.Registers[0x08FU]);
    const auto lighting =
        Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
    if (Oot3dPicaFragmentLightingEnabled(packet)) {
        const uint64_t lightingKey =
            ComputeOot3dPicaFragmentLightingStructuralKey(packet);
        key = HashWord(key, static_cast<uint32_t>(lightingKey));
        key = HashWord(key, static_cast<uint32_t>(lightingKey >> 32U));
    }
    key = HashWord(key, packet.Registers[0x0E0U]);
    key = HashWord(key, packet.Registers[0x104U] & 0x71U);
    key = HashWord(key, packet.Registers[0x100U] & 3U);
    for (uint16_t base : kTevStageRegisters) {
        key = HashWord(key, packet.Registers[base]);
        key = HashWord(key, packet.Registers[base + 1U]);
        key = HashWord(key, packet.Registers[base + 2U]);
        key = HashWord(key, packet.Registers[base + 4U]);
    }
    key = HashWord(key, state.Textures[0].Type);
    const bool shadowProcTexReferenced =
        lighting.Enabled && lighting.ShadowFactorEnabled &&
        lighting.ShadowTextureUnit == 3U;
    if ((packet.Registers[0x080U] & (1U << 10U)) != 0U &&
        (Oot3dPicaReferencesProceduralTexture(packet) ||
         shadowProcTexReferenced)) {
        for (uint16_t reg = 0x0A8U; reg <= 0x0ADU; ++reg) {
            key = HashWord(key, packet.Registers[reg]);
        }
        const auto hashTable = [&key](const auto& table) {
            for (const uint32_t word : table) {
                key = HashWord(key, word);
            }
        };
        hashTable(packet.ProcTexLuts.Noise);
        hashTable(packet.ProcTexLuts.ColorMap);
        hashTable(packet.ProcTexLuts.AlphaMap);
        hashTable(packet.ProcTexLuts.Color);
        hashTable(packet.ProcTexLuts.ColorDifference);
    }
    return key;
}

Oot3dPicaFragmentUniformState BuildOot3dPicaFragmentUniformState(
    const Oot3dPicaDrawPacket& packet) {
    Oot3dPicaFragmentUniformState uniforms;
    for (size_t stage = 0; stage < kTevStageRegisters.size(); ++stage) {
        uniforms.TevConstants[stage] = DecodeColor(
            packet.Registers[kTevStageRegisters[stage] + 3U]);
    }
    uniforms.CombinerBufferColor = DecodeColor(packet.Registers[0x0FDU]);
    uniforms.AlphaReference =
        static_cast<int32_t>((packet.Registers[0x104U] >> 8U) & 0xFFU);
    uniforms.ShadowTextureBias = static_cast<int32_t>(
        packet.Registers[0x08BU] & 0x00FFFFFEU);
    uniforms.ShadowOrthographic =
        static_cast<int32_t>(packet.Registers[0x08BU] & 1U);
    uniforms.ShadowBiasConstant =
        DecodeFloat16(packet.Registers[0x130U]);
    uniforms.ShadowBiasLinear =
        DecodeFloat16(packet.Registers[0x130U] >> 16U);
    uniforms.FogColor = DecodeColor(packet.Registers[0x0E1U]);
    static constexpr std::array<uint16_t, 3> kTextureLodRegisters{
        0x084U, 0x094U, 0x09CU};
    for (size_t texture = 0U; texture < kTextureLodRegisters.size();
         ++texture) {
        uint32_t raw = packet.Registers[kTextureLodRegisters[texture]] &
                       0x1FFFU;
        const int32_t signedRaw = (raw & 0x1000U) != 0U
                                      ? static_cast<int32_t>(raw) - 0x2000
                                      : static_cast<int32_t>(raw);
        uniforms.TextureLodBias[texture] =
            static_cast<float>(signedRaw) / 256.0F;
    }
    for (size_t index = 0; index < packet.FogLut.size(); ++index) {
        const uint32_t raw = packet.FogLut[index];
        const uint32_t value = (raw >> 13U) & 0x7FFU;
        const uint32_t differenceBits = raw & 0x1FFFU;
        const int32_t difference =
            (differenceBits & 0x1000U) != 0U
                ? static_cast<int32_t>(differenceBits | 0xFFFFE000U)
                : static_cast<int32_t>(differenceBits);
        uniforms.FogLut[index] = {
            static_cast<float>(value) / 2047.0F,
            static_cast<float>(difference) / 2047.0F};
    }
    uniforms.Lighting = BuildOot3dPicaFragmentLightingUniformState(packet);
    return uniforms;
}

bool GenerateOot3dPicaFragmentShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaGeneratedFragmentShader& shader, std::string* error,
    Oot3dPicaShaderBuildPurpose purpose) {
    shader = {};
    const auto lighting =
        Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
    const bool procTexEnabled =
        (packet.Registers[0x080U] & (1U << 10U)) != 0U;
    const bool shadowProcTexReferenced =
        lighting.Enabled && lighting.ShadowFactorEnabled &&
        lighting.ShadowTextureUnit == 3U;
    const bool procTexReferenced =
        Oot3dPicaReferencesProceduralTexture(packet) ||
        shadowProcTexReferenced;
    // Procedural LUT values are embedded in source, unlike the dynamically
    // bound lighting LUT image. A register-only cache cannot supply them.
    if (purpose == Oot3dPicaShaderBuildPurpose::OfflineSource &&
        procTexReferenced && procTexEnabled) {
        SetError(error, "offline register-only shader input lacks procedural LUT payload");
        return false;
    }
    std::string procTexSource;
    if (procTexReferenced && procTexEnabled &&
        !GenerateOot3dPicaProceduralTextureSampler(
            packet, procTexSource, error)) {
        return false;
    }
    bool bumpTextureSupported = true;
    const std::string bumpTextureSample =
        lighting.Enabled &&
                lighting.BumpMode != Fast::Oot3d::PicaLightingBumpMode::None
            ? TextureSampleExpression(lighting.BumpTextureUnit, state,
                                      bumpTextureSupported)
            : std::string{};
    if (!bumpTextureSupported) {
        SetError(error,
                 "PICA fragment-lighting bump texture type is not supported");
        return false;
    }
    bool shadowTextureSupported = true;
    std::string shadowTextureSample;
    if (lighting.Enabled && lighting.ShadowFactorEnabled) {
        shadowTextureSample = lighting.ShadowTextureUnit < 3U
            ? TextureSampleExpression(lighting.ShadowTextureUnit, state,
                                      shadowTextureSupported)
            : (procTexEnabled ? "pica_sample_proctex()" : "vec4(0.0)");
    }
    if (!shadowTextureSupported) {
        SetError(error,
                 "PICA fragment-lighting shadow texture type is not supported");
        return false;
    }
    std::string lightingDeclarations;
    std::string lightingMainBody;
    if (!GenerateOot3dPicaFragmentLightingSource(
            packet, bumpTextureSample, shadowTextureSample,
            lightingDeclarations,
            lightingMainBody, error, purpose)) {
        return false;
    }
    const bool fragmentLighting = Oot3dPicaFragmentLightingEnabled(packet);
    const uint32_t fogMode = packet.Registers[0x0E0U] & 7U;
    if (fogMode != 0U && fogMode != 5U) {
        SetError(error, fogMode == 7U
                            ? "PICA gas fragment shader path is not connected"
                            : "PICA fog mode is invalid");
        return false;
    }
    std::ostringstream source;
    auto& hooks = shader.Hooks;
    hooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.Outputs = {
        Oot3d::Renderer::kPicaFragmentOutputContractSchemaVersion,
        1U,
        0U,
        state.OutputMerger.FragmentOperationMode == 3U
            ? Oot3d::Renderer::PicaFragmentDepthOutput::FixedFunction
            : Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative,
        false,
        false,
    };
    hooks.Semantics =
        Oot3d::Renderer::PicaShaderSemantic::NormalQuaternion |
        Oot3d::Renderer::PicaShaderSemantic::NativeColorOutput |
        Oot3d::Renderer::PicaShaderSemantic::SecondaryFragmentColor |
        Oot3d::Renderer::PicaShaderSemantic::PrimaryColorInput |
        Oot3d::Renderer::PicaShaderSemantic::ViewVector |
        Oot3d::Renderer::PicaShaderSemantic::CombinerOutput;
    if (fragmentLighting) {
        hooks.Semantics |=
            Oot3d::Renderer::PicaShaderSemantic::MaterialNormal;
        if (lighting.BumpMode !=
                Fast::Oot3d::PicaLightingBumpMode::None &&
            lighting.BumpTextureUnit < hooks.TextureSamples.size() &&
            state.Textures[lighting.BumpTextureUnit].Enabled) {
            hooks.SampledTextureMask |= static_cast<uint8_t>(
                1U << lighting.BumpTextureUnit);
        }
        if (lighting.ShadowFactorEnabled &&
            lighting.ShadowTextureUnit < hooks.TextureSamples.size() &&
            state.Textures[lighting.ShadowTextureUnit].Enabled &&
            !(lighting.ShadowTextureUnit == 0U &&
              state.Textures[0].Type == 5U)) {
            hooks.SampledTextureMask |= static_cast<uint8_t>(
                1U << lighting.ShadowTextureUnit);
        }
    }
    source << "#version 450\n"
              "layout(location=0) in vec4 pica_primary_color;\n"
              "layout(location=1) in vec2 pica_texcoord0;\n"
              "layout(location=2) in vec2 pica_texcoord1;\n"
              "layout(location=3) in vec2 pica_texcoord2;\n"
              "layout(location=4) in float pica_texcoord0_w;\n"
              "layout(location=5) in vec4 pica_normquat;\n"
              "layout(location=6) in vec3 pica_view;\n";
    source << (state.Textures[0].Type == 2U
                   ? "layout(set=0,binding=1) uniform usampler2D pica_texture0;\n"
                   : "layout(set=0,binding=1) uniform sampler2D pica_texture0;\n")
           << "layout(set=0,binding=2) uniform sampler2D pica_texture1;\n"
              "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n"
              "layout(set=0,binding=4,std140) uniform PicaFragmentUniforms {\n"
              "    vec4 tev_constants[6];\n"
              "    vec4 combiner_buffer_color;\n"
              "    int alpha_reference;\n"
              "    float depth_scale;\n"
              "    float depth_offset;\n"
              "    int w_buffering;\n"
              "    vec4 fog_color;\n"
              "    vec4 fog_lut[64];\n"
              "    vec4 texture_lod_bias;\n"
              "    vec4 lighting_specular0[8];\n"
                  "    vec4 lighting_specular1[8];\n"
                  "    vec4 lighting_diffuse[8];\n"
                  "    vec4 lighting_ambient[8];\n"
                  "    vec4 lighting_position[8];\n"
                  "    vec4 lighting_spot_direction[8];\n"
                  "    vec4 lighting_attenuation[8];\n"
                  "    vec4 lighting_global_ambient;\n"
                  "    int shadow_texture_bias;\n"
                  "    int shadow_orthographic;\n"
                  "    float shadow_bias_constant;\n"
                  "    float shadow_bias_linear;\n"
                  "} fragment_uniforms;\n";
    if (state.OutputMerger.FragmentOperationMode == 3U) {
        source << "layout(set=0,binding=5,r32ui) uniform uimage2D pica_shadow_buffer;\n";
    }
    source << "layout(location=0) out vec4 pica_color;\n"
              "vec3 byteround3(vec3 value) { return floor(value * 255.0 + 0.5) / 255.0; }\n"
              "float byteround1(float value) { return floor(value * 255.0 + 0.5) / 255.0; }\n"
              "float pica_texture_lod(vec2 coord, vec2 size) {\n"
              "    vec2 scaled = coord * size;\n"
              "    vec2 delta = max(abs(dFdx(scaled)), abs(dFdy(scaled)));\n"
              "    return log2(max(delta.x, delta.y));\n"
              "}\n";
    if (state.Textures[0].Type == 2U) {
        source << Shadow2dHelpers(state);
    } else {
        source << "vec4 pica_sample_texture0(vec2 coord) {\n"
                  "    float lod = pica_texture_lod(coord, vec2(textureSize(pica_texture0, 0)));\n"
                  "    return textureLod(pica_texture0, coord, lod + fragment_uniforms.texture_lod_bias[0]);\n"
                  "}\n";
    }
    source << "vec4 pica_sample_texture1(vec2 coord) {\n"
              "    float lod = pica_texture_lod(coord, vec2(textureSize(pica_texture1, 0)));\n"
              "    return textureLod(pica_texture1, coord, lod + fragment_uniforms.texture_lod_bias[1]);\n"
              "}\n"
              "vec4 pica_sample_texture2(vec2 coord) {\n"
              "    float lod = pica_texture_lod(coord, vec2(textureSize(pica_texture2, 0)));\n"
              "    return textureLod(pica_texture2, coord, lod + fragment_uniforms.texture_lod_bias[2]);\n"
              "}\n";
    source << ShadowWriteHelpers(state);
    source << procTexSource;
    source << lightingDeclarations;
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::GlobalDeclarations)] =
        StreamOffset(source);
    source << "void main() {\n";
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::MainPrologue)] =
        StreamOffset(source);
    source << "    vec4 rounded_primary_color = vec4(byteround3(pica_primary_color.rgb), byteround1(pica_primary_color.a));\n"
              "    vec4 primary_fragment_color = vec4(0.0);\n"
              "    vec4 secondary_fragment_color = vec4(0.0);\n"
              "    vec4 combiner_buffer = vec4(0.0);\n"
              "    vec4 next_combiner_buffer = fragment_uniforms.combiner_buffer_color;\n"
              "    vec4 combiner_output = vec4(0.0);\n";
    source << lightingMainBody;
    source << "    // OOT3D_PICA_LIGHTING_EXTENSION_POINT";
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::PicaLighting)] =
        StreamOffset(source);
    hooks.Semantics |=
        fragmentLighting
            ? Oot3d::Renderer::PicaShaderSemantic::MaterialLightingPoint
            : Oot3d::Renderer::PicaShaderSemantic::VertexLightingPoint;
    source << "\n";

    bool supported = true;
    for (size_t stage = 0; stage < kTevStageRegisters.size(); ++stage) {
        const uint16_t base = kTevStageRegisters[stage];
        const uint32_t sources = packet.Registers[base];
        const uint32_t modifiers = packet.Registers[base + 1U];
        const uint32_t operations = packet.Registers[base + 2U];
        const uint32_t scales = packet.Registers[base + 4U];
        std::array<uint8_t, 3> colorSources{
            static_cast<uint8_t>(sources & 0xFU),
            static_cast<uint8_t>((sources >> 4U) & 0xFU),
            static_cast<uint8_t>((sources >> 8U) & 0xFU)};
        std::array<uint8_t, 3> alphaSources{
            static_cast<uint8_t>((sources >> 16U) & 0xFU),
            static_cast<uint8_t>((sources >> 20U) & 0xFU),
            static_cast<uint8_t>((sources >> 24U) & 0xFU)};
        if (stage == 0U) {
            for (size_t input = 0; input < 2U; ++input) {
                if (colorSources[input] == 0xFU) {
                    colorSources[input] = colorSources[2];
                }
                if (alphaSources[input] == 0xFU) {
                    alphaSources[input] = alphaSources[2];
                }
            }
        }
        std::array<std::string, 3> colorValues;
        std::array<std::string, 3> alphaValues;
        for (size_t input = 0; input < 3U; ++input) {
            const auto colorSource = SourceExpression(
                colorSources[input], stage, state, procTexEnabled, supported);
            const auto alphaSource = SourceExpression(
                alphaSources[input], stage, state, procTexEnabled, supported);
            colorValues[input] = ColorModifier(
                colorSource,
                static_cast<uint8_t>((modifiers >> (input * 4U)) & 0xFU),
                supported);
            const size_t alphaShift = 12U + input * 4U;
            alphaValues[input] = AlphaModifier(
                alphaSource,
                static_cast<uint8_t>((modifiers >> alphaShift) & 7U),
                supported);
        }
        const uint8_t colorOperation = operations & 0xFU;
        const uint8_t colorInputMask =
            TevOperationInputMask(colorOperation);
        hooks.SampledTextureMask |= SampledTextureMask(
            colorSources, colorInputMask, state);
        const uint8_t alphaOperation =
            static_cast<uint8_t>((operations >> 16U) & 0xFU);
        const uint8_t alphaInputMask = colorOperation == 7U
            ? 0U
            : TevOperationInputMask(alphaOperation);
        if (colorOperation != 7U) {
            hooks.SampledTextureMask |= SampledTextureMask(
                alphaSources, alphaInputMask, state);
        }
        if (InputMaskReferencesSource(colorSources, colorInputMask, 0U) ||
            InputMaskReferencesSource(alphaSources, alphaInputMask, 0U)) {
            hooks.Semantics |=
                Oot3d::Renderer::PicaShaderSemantic::
                    PrimaryColorConsumed;
        }
        if (InputMaskReferencesSource(colorSources, colorInputMask, 2U) ||
            InputMaskReferencesSource(alphaSources, alphaInputMask, 2U)) {
            hooks.Semantics |=
                Oot3d::Renderer::PicaShaderSemantic::
                    SecondaryFragmentColorConsumed;
        }
        const auto colorResult = ColorOperation(
            colorOperation, colorValues[0], colorValues[1], colorValues[2],
            supported);
        const auto alphaResult =
            colorOperation == 7U
                ? "color_output_" + std::to_string(stage) + ".r"
                : AlphaOperation(alphaOperation,
                                 alphaValues[0], alphaValues[1], alphaValues[2],
                                 supported);
        const uint32_t colorScaleBits = scales & 3U;
        const uint32_t alphaScaleBits = (scales >> 16U) & 3U;
        const uint32_t colorScale = colorScaleBits < 3U ? 1U << colorScaleBits : 1U;
        const uint32_t alphaScale = alphaScaleBits < 3U ? 1U << alphaScaleBits : 1U;
        source << "    vec3 color_output_" << stage
               << " = byteround3(clamp(" << colorResult
               << ", vec3(0.0), vec3(1.0)));\n"
               << "    float alpha_output_" << stage
               << " = byteround1(clamp(" << alphaResult
               << ", 0.0, 1.0));\n"
               << "    combiner_output = vec4(clamp(color_output_" << stage
               << " * " << colorScale
               << ".0, vec3(0.0), vec3(1.0)), clamp(alpha_output_" << stage
               << " * " << alphaScale << ".0, 0.0, 1.0));\n"
               << "    combiner_buffer = next_combiner_buffer;\n";
        if (stage < 4U &&
            (packet.Registers[0x0E0U] & (1U << (8U + stage))) != 0U) {
            source << "    next_combiner_buffer.rgb = combiner_output.rgb;\n";
        }
        if (stage < 4U &&
            (packet.Registers[0x0E0U] & (1U << (12U + stage))) != 0U) {
            source << "    next_combiner_buffer.a = combiner_output.a;\n";
        }
    }
    if (!supported) {
        SetError(error, DescribeUnsupportedTev(packet, state));
        return false;
    }

    for (uint8_t texture = 0U;
         texture < hooks.TextureSamples.size(); ++texture) {
        if (!hooks.SamplesTexture(texture)) {
            continue;
        }
        auto& sample = hooks.TextureSamples[texture];
        sample.Coordinate =
            texture == 2U && state.Texture2UsesCoordinate1 ? 1U : texture;
        sample.Operation =
            texture == 0U && state.Textures[texture].Type == 2U
                ? Oot3d::Renderer::
                      PicaTextureCoordinateOperation::Shadow2DNative
            : texture == 0U && state.Textures[texture].Type == 3U
                ? Oot3d::Renderer::
                      PicaTextureCoordinateOperation::ProjectedNativeVFlip
                : Oot3d::Renderer::
                      PicaTextureCoordinateOperation::NativeVFlip;
    }

    const uint32_t alphaTest = packet.Registers[0x104U];
    const bool alphaTestEnabled = (alphaTest & 1U) != 0U;
    const uint8_t alphaFunction = static_cast<uint8_t>((alphaTest >> 4U) & 7U);
    if (alphaTestEnabled) {
        source << "    int alpha_byte = int(combiner_output.a * 255.0);\n"
               << "    if (" << AlphaDiscardCondition(alphaFunction)
               << ") discard;\n";
    }
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::BeforeDepth)] =
        StreamOffset(source);
    source << "    float pica_z_over_w = -gl_FragCoord.z;\n"
              "    float pica_depth = pica_z_over_w * fragment_uniforms.depth_scale + fragment_uniforms.depth_offset;\n"
              "    if (fragment_uniforms.w_buffering != 0) pica_depth /= gl_FragCoord.w;\n";
    if (fogMode == 5U) {
        hooks.Semantics |= Oot3d::Renderer::PicaShaderSemantic::NativeFogFactor;
        const bool fogFlip = (packet.Registers[0x0E0U] & (1U << 16U)) != 0U;
        source << "    float fog_index = "
               << (fogFlip ? "(1.0 - pica_depth)" : "pica_depth")
               << " * 128.0;\n"
                  "    float fog_i = clamp(floor(fog_index), 0.0, 127.0);\n"
                  "    int fog_entry_index = int(fog_i);\n"
                  "    vec4 fog_pair = fragment_uniforms.fog_lut[fog_entry_index >> 1];\n"
                  "    vec2 fog_entry = (fog_entry_index & 1) == 0 ? fog_pair.xy : fog_pair.zw;\n"
                  "    float fog_factor = clamp(fog_entry.x + fog_entry.y * (fog_index - fog_i), 0.0, 1.0);\n"
                  "    combiner_output.rgb = mix(fragment_uniforms.fog_color.rgb, combiner_output.rgb, fog_factor);\n";
    }
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::BeforeNativeColor)] =
        StreamOffset(source);
    if (state.OutputMerger.FragmentOperationMode == 3U) {
        source <<
            "    uint pica_shadow_depth = uint(clamp(pica_depth, 0.0, 1.0) * 16777215.0);\n"
            "    uint pica_shadow_penumbra = uint(combiner_output.g * 255.0);\n"
            "    ivec2 pica_shadow_coordinate = ivec2(gl_FragCoord.xy);\n"
            "    uint pica_shadow_old = imageLoad(pica_shadow_buffer, pica_shadow_coordinate).x;\n"
            "    uint pica_shadow_new;\n"
            "    uint pica_shadow_observed;\n"
            "    do {\n"
            "        pica_shadow_new = pica_update_shadow2d(pica_shadow_old, pica_shadow_depth, pica_shadow_penumbra);\n"
            "        pica_shadow_observed = imageAtomicCompSwap(pica_shadow_buffer, pica_shadow_coordinate, pica_shadow_old, pica_shadow_new);\n"
            "        if (pica_shadow_observed == pica_shadow_old) break;\n"
            "        pica_shadow_old = pica_shadow_observed;\n"
            "    } while (true);\n"
            "    pica_color = vec4(0.0);\n";
    } else {
        source <<
            "    gl_FragDepth = pica_depth;\n"
            "    pica_color = vec4(byteround3(combiner_output.rgb), byteround1(combiner_output.a));\n";
    }
    hooks.Offsets[static_cast<size_t>(
        Oot3d::Renderer::PicaShaderHook::MainEpilogue)] =
        StreamOffset(source);
    source << "}\n";

    if (purpose == Oot3dPicaShaderBuildPurpose::RuntimeDraw)
        shader.Uniforms = BuildOot3dPicaFragmentUniformState(packet);
    shader.StateKey = ComputeOot3dPicaFragmentShaderStateKey(packet, state);
    shader.Source = source.str();
    shader.SourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(shader.Source);
    hooks.SourceSize = shader.Source.size();
    return true;
}

} // namespace Oot3dNativeGame
