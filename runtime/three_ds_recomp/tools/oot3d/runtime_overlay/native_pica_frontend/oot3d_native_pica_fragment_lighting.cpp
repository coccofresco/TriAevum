#include "oot3d_native_pica_fragment_lighting.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Oot3dNativeGame {
namespace {

constexpr std::array<uint8_t, 7> kSamplerShifts{ 0, 4, 8, 12, 16, 20, 24 };

void SetError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

uint64_t HashWord(uint64_t hash, uint32_t word) {
    constexpr uint64_t prime = 1099511628211ULL;
    for (size_t byte = 0; byte < sizeof(word); ++byte) {
        hash ^= static_cast<uint8_t>(word >> (byte * 8U));
        hash *= prime;
    }
    return hash;
}

float DecodeFloat(uint32_t raw, uint32_t exponentBits, uint32_t mantissaBits) {
    const uint32_t exponentMask = (1U << exponentBits) - 1U;
    const uint32_t mantissaMask = (1U << mantissaBits) - 1U;
    const uint32_t exponent = (raw >> mantissaBits) & exponentMask;
    const uint32_t mantissa = raw & mantissaMask;
    const bool negative = ((raw >> (exponentBits + mantissaBits)) & 1U) != 0U;
    const int bias = (1 << (exponentBits - 1U)) - 1;
    float value = 0.0F;
    if (exponent == exponentMask) value = mantissa == 0U ? INFINITY : NAN;
    else if (exponent == 0U)
        value = std::ldexp(static_cast<float>(mantissa),
                           1 - bias - static_cast<int>(mantissaBits));
    else
        value = std::ldexp(1.0F + static_cast<float>(mantissa) /
                                      static_cast<float>(1U << mantissaBits),
                           static_cast<int>(exponent) - bias);
    return negative ? -value : value;
}

std::array<float, 4> DecodeColor(uint32_t raw) {
    return { static_cast<float>((raw >> 20U) & 0xFFU) / 255.0F,
             static_cast<float>((raw >> 10U) & 0xFFU) / 255.0F,
             static_cast<float>(raw & 0xFFU) / 255.0F, 1.0F };
}

float DecodeSpot(uint32_t raw) {
    int32_t value = static_cast<int32_t>(raw & 0x1FFFU);
    if ((value & 0x1000) != 0) value -= 0x2000;
    return -static_cast<float>(value) / 2048.0F;
}

float LutScale(uint32_t raw) {
    switch (raw & 7U) {
    case 1: return 2.0F;
    case 2: return 4.0F;
    case 3: return 8.0F;
    case 6: return 0.25F;
    case 7: return 0.5F;
    default: return 1.0F;
    }
}

bool LutAvailable(uint8_t environment, size_t sampler) {
    switch (environment) {
    case 0: return sampler == 0 || sampler == 2 || sampler == 6;
    case 1: return sampler == 2 || sampler == 3 || sampler == 6;
    case 2: return sampler == 0 || sampler == 1 || sampler == 6;
    case 3: return sampler == 0 || sampler == 1 || sampler == 3;
    case 4: return sampler != 3;
    case 5: return sampler != 1;
    case 6: return sampler != 4 && sampler != 5;
    case 8: return true;
    default: return false;
    }
}

std::string FloatLiteral(float value) {
    std::ostringstream stream;
    stream.precision(9);
    stream << value;
    if (stream.str().find('.') == std::string::npos) stream << ".0";
    return stream.str();
}

std::string LutInputExpression(uint32_t selector) {
    switch (selector & 7U) {
    case 0: return "dot(normal, half_vector)";
    case 1: return "dot(view_vector, half_vector)";
    case 2: return "dot(normal, view_vector)";
    case 3: return "dot(light_vector, normal)";
    case 4: return "-dot(light_vector, spot_direction)";
    case 5: return "dot(normalize(-light_position), spot_direction)";
    default: return "0.0";
    }
}

std::string LutCall(size_t slot, size_t sampler, uint32_t absRegister,
                    uint32_t inputRegister, uint32_t scaleRegister) {
    const uint8_t shift = kSamplerShifts[sampler];
    const bool absolute = ((absRegister >> (shift + 1U)) & 1U) == 0U;
    const uint32_t selector = (inputRegister >> shift) & 7U;
    const float scale = LutScale((scaleRegister >> shift) & 7U);
    return FloatLiteral(scale) + " * oot3d_lighting_lut(" +
           std::to_string(slot) + ", " + LutInputExpression(selector) +
           ", " + (absolute ? "true" : "false") + ")";
}

bool TextureExpression(const Oot3dPicaDecodedDrawState& state,
                       size_t texture, std::string& expression,
                       const char* disabledMessage,
                       const char* unsupportedMessage,
                       std::string* error) {
    if (texture >= state.Textures.size() ||
        !state.Textures[texture].Enabled) {
        SetError(error, disabledMessage);
        return false;
    }
    const size_t coordinate =
        texture == 2U && state.Texture2UsesCoordinate1 ? 1U : texture;
    if (texture == 0U && state.Textures[texture].Type == 3U) {
        expression =
            "textureProj(pica_texture0, vec3(pica_texcoord0.x, "
            "pica_texcoord0_w - pica_texcoord0.y, pica_texcoord0_w))";
        return true;
    }
    if (texture == 0U && state.Textures[texture].Type != 0U) {
        SetError(error, unsupportedMessage);
        return false;
    }
    const std::string texcoord =
        "pica_texcoord" + std::to_string(coordinate);
    expression = "texture(pica_texture" + std::to_string(texture) +
                 ", vec2(" + texcoord + ".x, 1.0 - " + texcoord + ".y))";
    return true;
}

} // namespace

bool BuildOot3dPicaFragmentLightingShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaFragmentLightingShader& shader, std::string* error) {
    shader = {};
    const auto& registers = packet.Registers;
    shader.Enabled = (registers[0x08FU] & 1U) != 0U &&
                     (registers[0x1C6U] & 1U) == 0U;
    if (!shader.Enabled) return true;

    const uint32_t config0 = registers[0x1C3U];
    const uint32_t config1 = registers[0x1C4U];
    const uint32_t bumpMode = (config0 >> 28U) & 3U;
    const size_t bumpTexture = (config0 >> 22U) & 3U;
    std::string bumpSample;
    if (bumpMode == 3U) {
        SetError(error, "PICA bump mode is not supported");
        return false;
    }
    if (bumpMode != 0U &&
        !TextureExpression(state, bumpTexture, bumpSample,
                           "PICA bump texture is not enabled",
                           "PICA bump texture0 type is not supported", error))
        return false;
    const bool shadowEnabled = (config0 & 1U) != 0U;
    const size_t shadowTexture = (config0 >> 24U) & 3U;
    std::string shadowSample;
    if (shadowEnabled) {
        if (shadowTexture == 0U && state.Textures[0].Enabled &&
            state.Textures[0].Type == 5U) {
            shadowSample =
                "oot3d_sample_shadow2d(pica_texcoord0, pica_texcoord0_w)";
        } else if (!TextureExpression(
                       state, shadowTexture, shadowSample,
                       "PICA lighting shadow texture is not enabled",
                       "PICA lighting shadow texture type is not supported",
                       error)) {
            return false;
        }
    }
    const uint32_t permutation = registers[0x1D9U];
    const size_t lightCount = (registers[0x1C2U] & 7U) + 1U;
    const uint8_t environment = static_cast<uint8_t>((config0 >> 4U) & 0xFU);
    const uint32_t absRegister = registers[0x1D0U];
    const uint32_t inputRegister = registers[0x1D1U];
    const uint32_t scaleRegister = registers[0x1D2U];

    constexpr uint64_t offset = 1469598103934665603ULL;
    shader.StructuralKey = HashWord(offset, registers[0x08FU] & 1U);
    for (uint32_t word : { registers[0x1C2U] & 7U, config0, config1,
                           registers[0x1C6U] & 1U, absRegister,
                           inputRegister, scaleRegister, permutation }) {
        shader.StructuralKey = HashWord(shader.StructuralKey, word);
    }
    if (bumpMode != 0U) {
        shader.StructuralKey = HashWord(
            shader.StructuralKey,
            static_cast<uint32_t>(state.Textures[bumpTexture].Enabled) |
                (static_cast<uint32_t>(state.Textures[bumpTexture].Type) << 1U) |
                (static_cast<uint32_t>(state.Texture2UsesCoordinate1) << 8U));
    }
    if (shadowEnabled) {
        shader.StructuralKey = HashWord(
            shader.StructuralKey,
            static_cast<uint32_t>(state.Textures[shadowTexture].Enabled) |
                (static_cast<uint32_t>(state.Textures[shadowTexture].Type) << 1U) |
                (static_cast<uint32_t>(state.Texture2UsesCoordinate1) << 8U));
    }

    shader.Uniforms.GlobalAmbient = DecodeColor(registers[0x1C0U]);
    for (size_t light = 0; light < 8U; ++light) {
        const size_t base = 0x140U + light * 0x10U;
        shader.Uniforms.Specular0[light] = DecodeColor(registers[base]);
        shader.Uniforms.Specular1[light] = DecodeColor(registers[base + 1U]);
        shader.Uniforms.Diffuse[light] = DecodeColor(registers[base + 2U]);
        shader.Uniforms.Ambient[light] = DecodeColor(registers[base + 3U]);
        const uint32_t xy = registers[base + 4U];
        shader.Uniforms.Position[light] = {
            DecodeFloat(xy, 5U, 10U), DecodeFloat(xy >> 16U, 5U, 10U),
            DecodeFloat(registers[base + 5U], 5U, 10U), 0.0F };
        const uint32_t spot = registers[base + 6U];
        shader.Uniforms.SpotDirection[light] = {
            DecodeSpot(spot), DecodeSpot(spot >> 16U),
            DecodeSpot(registers[base + 7U]), 0.0F };
        shader.Uniforms.Attenuation[light] = {
            DecodeFloat(registers[base + 9U], 7U, 12U),
            DecodeFloat(registers[base + 10U], 7U, 12U), 0.0F, 0.0F };
        shader.StructuralKey = HashWord(shader.StructuralKey,
                                        registers[base + 8U]);
    }
    shader.Uniforms.LutTables = packet.LightingLuts;

    shader.UniformDeclarations =
        "    vec4 lighting_global_ambient;\n"
        "    vec4 light_specular0[8];\n"
        "    vec4 light_specular1[8];\n"
        "    vec4 light_diffuse[8];\n"
        "    vec4 light_ambient[8];\n"
        "    vec4 light_position[8];\n"
        "    vec4 light_spot_direction[8];\n"
        "    vec4 light_attenuation[8];\n"
        "    uvec4 lighting_lut_raw[1408];\n";
    shader.Helpers = R"glsl(
vec3 oot3d_quaternion_rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
float oot3d_lighting_lut(int slot, float position, bool absolute_input) {
    float scaled = absolute_input ? clamp(abs(position), 0.0, 1.0) * 256.0
                                  : clamp(position, -1.0, 1.0) * 128.0;
    int signed_index = absolute_input ? clamp(int(scaled), 0, 255)
                                      : clamp(int(scaled), -128, 127);
    float delta = scaled - float(signed_index);
    int index = signed_index < 0 ? signed_index + 256 : signed_index;
    int flat_index = slot * 256 + index;
    uint raw = fragment_uniforms.lighting_lut_raw[flat_index >> 2][flat_index & 3];
    int signed_delta = int((raw >> 12u) & 0xfffu);
    if ((signed_delta & 0x800) != 0) signed_delta -= 0x1000;
    return float(raw & 0xfffu) / 4095.0 +
           float(signed_delta) / 2047.0 * delta;
}
)glsl";

    std::ostringstream body;
    if (bumpMode == 1U) {
        body << "    vec3 surface_normal = 2.0 * " << bumpSample
             << ".rgb - 1.0;\n";
        if (((config0 >> 30U) & 1U) == 0U)
            body << "    surface_normal.z = sqrt(max(1.0 - dot(surface_normal.xy, surface_normal.xy), 0.0));\n";
        body << "    vec3 surface_tangent = vec3(1.0, 0.0, 0.0);\n";
    } else if (bumpMode == 2U) {
        body << "    vec3 surface_tangent = 2.0 * " << bumpSample
             << ".rgb - 1.0;\n"
                "    vec3 surface_normal = vec3(0.0, 0.0, 1.0);\n";
    } else {
        body << "    vec3 surface_normal = vec3(0.0, 0.0, 1.0);\n"
                "    vec3 surface_tangent = vec3(1.0, 0.0, 0.0);\n";
    }
    body << "    vec4 normalized_normquat = normalize(pica_normquat);\n"
            "    vec3 normal = normalize(oot3d_quaternion_rotate(normalized_normquat, surface_normal));\n"
            "    vec3 tangent = normalize(oot3d_quaternion_rotate(normalized_normquat, surface_tangent));\n"
            "    oot3d_normal_guide = normal;\n"
            "    // OOT3D_PICA_NORMAL_GUIDE_READY\n"
            "    vec3 view_vector = normalize(pica_view);\n"
            "    vec4 diffuse_sum = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    vec3 ambient_sum = vec3(0.0);\n"
            "    vec4 specular_sum = vec4(0.0, 0.0, 0.0, 1.0);\n";
    if (shadowEnabled) {
        body << "    vec4 lighting_shadow = "
             << (((config0 >> 18U) & 1U) != 0U ? "vec4(1.0) - " : "")
             << shadowSample << ";\n";
    } else {
        body << "    vec4 lighting_shadow = vec4(1.0);\n";
    }
    for (size_t slot = 0; slot < lightCount; ++slot) {
        const size_t light = (permutation >> (slot * 4U)) & 7U;
        const size_t base = 0x140U + light * 0x10U;
        const uint32_t lightConfig = registers[base + 8U];
        if ((lightConfig & 0xCU) != 0U) {
            SetError(error, "PICA fragment lighting geometric factors are not connected");
            return false;
        }
        const bool directional = (lightConfig & 1U) != 0U;
        const bool twoSided = (lightConfig & 2U) != 0U;
        const bool lightReceivesShadow =
            shadowEnabled && ((config1 >> light) & 1U) == 0U;
        const bool primaryShadow = lightReceivesShadow &&
                                   ((config0 >> 16U) & 1U) != 0U;
        const bool secondaryShadow = lightReceivesShadow &&
                                     ((config0 >> 17U) & 1U) != 0U;
        body << "    {\n"
             << "        const int light = " << light << ";\n"
             << "        vec3 light_position = fragment_uniforms.light_position[light].xyz;\n"
             << "        vec3 light_vector = normalize("
             << (directional ? "light_position" : "light_position + pica_view")
             << ");\n"
             << "        vec3 spot_direction = normalize(fragment_uniforms.light_spot_direction[light].xyz);\n"
             << "        vec3 half_vector = normalize(view_vector + light_vector);\n"
             << "        float light_normal = dot(light_vector, normal);\n"
             << "        float diffuse_factor = "
             << (twoSided ? "abs(light_normal)" : "max(light_normal, 0.0)") << ";\n"
             << "        float highlight = sign(max(light_normal, 0.0));\n"
             << "        float distribution0 = 1.0;\n"
             << "        float distribution1 = 1.0;\n"
             << "        vec3 reflection = vec3(1.0);\n"
             << "        float spot = 1.0;\n"
             << "        float distance_attenuation = 1.0;\n";
        if (LutAvailable(environment, 0) && ((config1 >> 16U) & 1U) == 0U)
            body << "        distribution0 = " << LutCall(0, 0, absRegister, inputRegister, scaleRegister) << ";\n";
        if (LutAvailable(environment, 1) && ((config1 >> 17U) & 1U) == 0U)
            body << "        distribution1 = " << LutCall(1, 1, absRegister, inputRegister, scaleRegister) << ";\n";
        if (LutAvailable(environment, 6) && ((config1 >> 22U) & 1U) == 0U)
            body << "        reflection.r = " << LutCall(5, 6, absRegister, inputRegister, scaleRegister) << ";\n";
        if (LutAvailable(environment, 5) && ((config1 >> 21U) & 1U) == 0U)
            body << "        reflection.g = " << LutCall(4, 5, absRegister, inputRegister, scaleRegister) << ";\n";
        else body << "        reflection.g = reflection.r;\n";
        if (LutAvailable(environment, 4) && ((config1 >> 20U) & 1U) == 0U)
            body << "        reflection.b = " << LutCall(3, 4, absRegister, inputRegister, scaleRegister) << ";\n";
        else body << "        reflection.b = reflection.r;\n";
        if (LutAvailable(environment, 2) &&
            ((config1 >> (8U + light)) & 1U) == 0U)
            body << "        spot = " << LutCall(6 + light, 2, absRegister, inputRegister, scaleRegister) << ";\n";
        if (((config1 >> (24U + light)) & 1U) == 0U)
            body << "        float distance_index = clamp(fragment_uniforms.light_attenuation[light].x + fragment_uniforms.light_attenuation[light].y * length(light_position + pica_view), 0.0, 1.0);\n"
                 << "        distance_attenuation = oot3d_lighting_lut(" << (14 + light) << ", distance_index, true);\n";
        body << "        float attenuation = spot * distance_attenuation;\n"
             << "        diffuse_sum.rgb += (fragment_uniforms.light_diffuse[light].rgb * diffuse_factor"
             << (primaryShadow ? " * lighting_shadow.rgb" : "")
             << " + fragment_uniforms.light_ambient[light].rgb) * attenuation;\n"
             << "        ambient_sum += fragment_uniforms.light_ambient[light].rgb * attenuation;\n"
             << "        specular_sum.rgb += (fragment_uniforms.light_specular0[light].rgb * distribution0 + reflection * fragment_uniforms.light_specular1[light].rgb * distribution1) * highlight * attenuation"
             << (secondaryShadow ? " * lighting_shadow.rgb" : "")
             << ";\n";
        if (LutAvailable(environment, 3) && ((config1 >> 19U) & 1U) == 0U) {
            const std::string fresnel = LutCall(2, 3, absRegister, inputRegister, scaleRegister);
            const uint32_t selector = (config0 >> 2U) & 3U;
            if ((selector & 1U) != 0U) body << "        diffuse_sum.a = " << fresnel << ";\n";
            if ((selector & 2U) != 0U) body << "        specular_sum.a = " << fresnel << ";\n";
        }
        body << "    }\n";
    }
    if (shadowEnabled && ((config0 >> 19U) & 1U) != 0U) {
        if (((config0 >> 2U) & 1U) != 0U)
            body << "    diffuse_sum.a *= lighting_shadow.a;\n";
        if (((config0 >> 3U) & 1U) != 0U)
            body << "    specular_sum.a *= lighting_shadow.a;\n";
    }
    body << "    ambient_sum += fragment_uniforms.lighting_global_ambient.rgb;\n"
            "    diffuse_sum.rgb += fragment_uniforms.lighting_global_ambient.rgb;\n"
            "    const vec3 oot3d_ao_luma = vec3(0.2126, 0.7152, 0.0722);\n"
            "    float oot3d_ambient_luma = dot(clamp(ambient_sum, vec3(0.0), vec3(1.0)), oot3d_ao_luma);\n"
            "    float oot3d_diffuse_luma = dot(clamp(diffuse_sum.rgb, vec3(0.0), vec3(1.0)), oot3d_ao_luma);\n"
            "    float oot3d_ao_response = clamp(oot3d_ambient_luma / max(oot3d_diffuse_luma, 0.0001), 0.0, 1.0);\n"
            "    vec3 oot3d_ao_response_rgb = clamp(clamp(ambient_sum, vec3(0.0), vec3(1.0)) / max(clamp(diffuse_sum.rgb, vec3(0.0), vec3(1.0)), vec3(0.0001)), vec3(0.0), vec3(1.0));\n"
            "    // OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY\n"
            "    primary_fragment_color = clamp(diffuse_sum, vec4(0.0), vec4(1.0));\n"
            "    secondary_fragment_color = clamp(specular_sum, vec4(0.0), vec4(1.0));\n"
            "    // OOT3D_PICA_MATERIAL_TOON_POINT\n";
    shader.Body = body.str();
    return true;
}

} // namespace Oot3dNativeGame
