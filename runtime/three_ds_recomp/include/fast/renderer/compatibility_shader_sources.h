#pragma once
// Maintained sources shared by runtime and offline compilation.
#include "fast/renderer/compatibility_combiner.h"
#include <sstream>
#include <string>
namespace Fast::Renderer {
inline bool IsOot3dPicaTexture2Shader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

inline bool IsOot3dShadow2dShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D;
}

inline bool IsPicaTextureEnvShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D ||
           shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY ||
           shaderId == SHADER_ID_PICA_TEXTURE2_MULT_ADD;
}

inline bool IsPicaTextureEnvPostMultiplyShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY;
}

inline bool IsOot3dPicaFogShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_FOG || IsPicaTextureEnvShader(shaderId);
}

inline bool IsOot3dPicaAlphaTestShader(int16_t shaderId) {
    return shaderId == SHADER_ID_PICA_ALPHA_TEST || IsOot3dPicaFogShader(shaderId);
}

inline std::string ShaderInputName(uint32_t item) {
    if (item >= SHADER_INPUT_1 && item <= SHADER_INPUT_7) {
        return "input" + std::to_string(item - SHADER_INPUT_1 + 1);
    }
    return {};
}

inline std::string ShaderItemExpression(uint32_t item, bool withAlpha, bool onlyAlpha,
                                 bool inputsHaveAlpha, bool firstCycle,
                                 bool hintSingleElement) {
    const std::string input = ShaderInputName(item);
    if (!input.empty()) {
        if (onlyAlpha) {
            return input + ".a";
        }
        return withAlpha || !inputsHaveAlpha ? input : input + ".rgb";
    }

    if (onlyAlpha) {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_TEXEL0:
            case SHADER_TEXEL0A:
                return firstCycle ? "texVal0.a" : "texVal1.a";
            case SHADER_TEXEL1:
            case SHADER_TEXEL1A:
                return firstCycle ? "texVal1.a" : "texVal0.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return "drawRandom";
            default:
                return "0.0";
        }
    }

    const char* zero = withAlpha ? "vec4(0.0)" : "vec3(0.0)";
    const char* one = withAlpha ? "vec4(1.0)" : "vec3(1.0)";
    switch (item) {
        case SHADER_0:
            return zero;
        case SHADER_1:
            return one;
        case SHADER_TEXEL0:
            return firstCycle ? (withAlpha ? "texVal0" : "texVal0.rgb")
                              : (withAlpha ? "texVal1" : "texVal1.rgb");
        case SHADER_TEXEL1:
            return firstCycle ? (withAlpha ? "texVal1" : "texVal1.rgb")
                              : (withAlpha ? "texVal0" : "texVal0.rgb");
        case SHADER_TEXEL0A: {
            const char* source = firstCycle ? "texVal0.a" : "texVal1.a";
            if (hintSingleElement) {
                return source;
            }
            return std::string(withAlpha ? "vec4(" : "vec3(") + source + ")";
        }
        case SHADER_TEXEL1A: {
            const char* source = firstCycle ? "texVal1.a" : "texVal0.a";
            if (hintSingleElement) {
                return source;
            }
            return std::string(withAlpha ? "vec4(" : "vec3(") + source + ")";
        }
        case SHADER_COMBINED:
            return withAlpha ? "texel" : "texel.rgb";
        case SHADER_NOISE:
            return withAlpha ? "vec4(drawRandom)" : "vec3(drawRandom)";
        default:
            return zero;
    }
}

inline std::string BuildCombinerFormula(const CCFeatures& features, int cycle, bool alphaChannel,
                                 bool withAlpha) {
    const int channel = alphaChannel ? 1 : 0;
    const int* combine = features.c[cycle][channel];
    const bool onlyAlpha = alphaChannel;
    const bool firstCycle = cycle == 0;
    const auto item = [&](int index, bool hintSingleElement = false) {
        return ShaderItemExpression(combine[index], withAlpha, onlyAlpha, features.opt_alpha,
                                    firstCycle, hintSingleElement);
    };
    if (features.do_single[cycle][channel]) {
        return item(3);
    }
    if (features.do_multiply[cycle][channel]) {
        return item(0) + " * " + item(2, true);
    }
    if (features.do_mix[cycle][channel]) {
        return "mix(" + item(1) + ", " + item(0) + ", " + item(2, true) + ")";
    }
    return "(" + item(0) + " - " + item(1) + ") * " + item(2, true) + " + " +
           item(3);
}

inline std::string BuildCompatibilityVertexShader(const CCFeatures& features) {
    std::ostringstream source;
    source << "#version 450\n"
              "layout(location = 0) in vec4 aPosition;\n"
              "layout(push_constant) uniform TransformState {\n"
              "    mat4 modelViewProjection;\n"
              "} transformState;\n";
    if (features.usedTextures[0]) {
        source << "layout(location = 1) in vec2 aTexCoord0;\n"
                  "layout(location = 0) out vec2 vTexCoord0;\n";
    }
    if (features.usedTextures[1]) {
        source << "layout(location = 2) in vec2 aTexCoord1;\n"
                  "layout(location = 1) out vec2 vTexCoord1;\n";
    }
    if (IsOot3dPicaTexture2Shader(features.shader_id)) {
        source << "layout(location = 3) in vec2 aTexCoord2;\n"
                  "layout(location = 2) out vec2 vTexCoord2;\n";
    }
    if (features.opt_fog) {
        source << "layout(location = 4) in vec4 aFog;\n"
                  "layout(location = 3) out vec4 vFog;\n";
    }
    if (features.opt_grayscale) {
        source << "layout(location = 5) in vec4 aGrayscale;\n"
                  "layout(location = 4) out vec4 vGrayscale;\n";
    }
    for (uint32_t input = 0; input < features.numInputs; ++input) {
        source << "layout(location = " << (6 + input) << ") in vec"
               << (features.opt_alpha ? 4 : 3) << " aInput" << (input + 1) << ";\n"
               << "layout(location = " << (5 + input) << ") out vec"
               << (features.opt_alpha ? 4 : 3) << " vInput" << (input + 1) << ";\n";
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        source << "layout(location = 14) in vec3 aShadowCoordinate;\n"
                  "layout(location = 13) out vec3 vShadowCoordinate;\n";
    }
    source << "void main() {\n"
              "    gl_Position = transformState.modelViewProjection * aPosition;\n";
    if (features.usedTextures[0]) {
        source << "    vTexCoord0 = aTexCoord0;\n";
    }
    if (features.usedTextures[1]) {
        source << "    vTexCoord1 = aTexCoord1;\n";
    }
    if (IsOot3dPicaTexture2Shader(features.shader_id)) {
        source << "    vTexCoord2 = aTexCoord2;\n";
    }
    if (features.opt_fog) {
        source << "    vFog = aFog;\n";
    }
    if (features.opt_grayscale) {
        source << "    vGrayscale = aGrayscale;\n";
    }
    for (uint32_t input = 0; input < features.numInputs; ++input) {
        source << "    vInput" << (input + 1) << " = aInput" << (input + 1) << ";\n";
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        source << "    vShadowCoordinate = aShadowCoordinate;\n";
    }
    source << "}\n";
    return source.str();
}

inline std::string BuildCompatibilityFragmentShader(const CCFeatures& features, bool srgb) {
    const bool picaTextureEnv = IsPicaTextureEnvShader(features.shader_id);
    const bool picaPostMultiply =
        IsPicaTextureEnvPostMultiplyShader(features.shader_id);
    const bool picaTexture2 = IsOot3dPicaTexture2Shader(features.shader_id);
    const bool picaShadow2d = IsOot3dShadow2dShader(features.shader_id);
    const bool picaFog = features.opt_fog && IsOot3dPicaFogShader(features.shader_id);
    const bool picaAlphaTest =
        features.opt_alpha_threshold && IsOot3dPicaAlphaTestShader(features.shader_id);

    std::ostringstream source;
    source << "#version 450\n";
    if (features.usedTextures[0]) {
        source << "layout(set = 0, binding = 0) uniform sampler2D uTex0;\n"
                  "layout(location = 0) in vec2 vTexCoord0;\n";
    }
    if (features.usedTextures[1]) {
        source << "layout(set = 0, binding = 1) uniform sampler2D uTex1;\n"
                  "layout(location = 1) in vec2 vTexCoord1;\n";
    }
    if (picaTexture2) {
        source << "layout(set = 0, binding = 2) uniform sampler2D uTex2;\n"
                  "layout(location = 2) in vec2 vTexCoord2;\n";
    }
    if (picaShadow2d) {
        source << "layout(set = 0, binding = 3) uniform usampler2D uOot3dShadow2d;\n";
    }
    if (features.opt_fog) {
        source << "layout(location = 3) in vec4 vFog;\n";
    }
    if (features.opt_grayscale) {
        source << "layout(location = 4) in vec4 vGrayscale;\n";
    }
    for (int input = 0; input < features.numInputs; ++input) {
        source << "layout(location = " << (5 + input) << ") in vec"
               << (features.opt_alpha ? 4 : 3) << " vInput" << (input + 1) << ";\n";
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        source << "layout(location = 13) in vec3 vShadowCoordinate;\n";
    }
    source << R"glsl(
layout(std430, set = 0, binding = 4) readonly buffer DrawUniformState {
    ivec4 textureWidth;
    ivec4 textureHeight;
    ivec4 textureFiltering;
    uint frameCount;
    float noiseScale;
    float primDepth;
    int fogFlip;
    int alphaTestEnabled;
    int alphaTestFunction;
    int alphaTestReference;
    int shadowTextureBias;
    int shadowOrthographic;
    int shadowInvert;
    int reserved;
    uint fogLut[128];
} draw;
layout(location = 0) out vec4 outColor;

float randomValue(vec3 value) {
    float randomSeed = dot(sin(value), vec3(12.9898, 78.233, 37.719));
    return fract(sin(randomSeed) * 143758.5453);
}

vec3 picaByteRound(vec3 value) {
    return round(value * 255.0) * (1.0 / 255.0);
}

vec4 picaByteRound(vec4 value) {
    return round(value * 255.0) * (1.0 / 255.0);
}

vec4 fromLinear(vec4 linearRgb) {
    bvec3 cutoff = lessThan(linearRgb.rgb, vec3(0.0031308));
    vec3 higher = vec3(1.055) * pow(linearRgb.rgb, vec3(1.0 / 2.4)) - vec3(0.055);
    vec3 lower = linearRgb.rgb * vec3(12.92);
    return vec4(mix(higher, lower, cutoff), linearRgb.a);
}

float decodePicaFogValue(uint word) {
    return float((word >> 13u) & 0x7FFu) * (1.0 / 2048.0);
}

float decodePicaFogDiff(uint word) {
    uint raw = word & 0x1FFFu;
    uint encoded = raw < 4096u ? raw + 4096u : raw - 4096u;
    return float(encoded) * (1.0 / 2048.0) - 2.0;
}

float samplePicaFogFactor() {
    float depth = clamp(gl_FragCoord.z, 0.0, 1.0);
    float lutIndex = (draw.fogFlip != 0 ? 1.0 - depth : depth) * 128.0;
    float lutFloor = clamp(floor(lutIndex), 0.0, 127.0);
    uint word = draw.fogLut[int(lutFloor)];
    return clamp(decodePicaFogValue(word) +
                 decodePicaFogDiff(word) * (lutIndex - lutFloor), 0.0, 1.0);
}
)glsl";
    if (picaShadow2d) {
        source << R"glsl(

float compareOot3dShadow2d(uint pixel, uint z) {
    uint depth24 = pixel >> 8u;
    uint alpha8 = pixel & 0xFFu;
    return depth24 <= z ? 0.0 : float(alpha8) * (1.0 / 255.0);
}

float sampleOot3dShadow2dTap(ivec2 uv, uint z) {
    ivec2 size = textureSize(uOot3dShadow2d, 0);
    if (any(lessThan(uv, ivec2(0))) || any(greaterThanEqual(uv, size))) {
        return 1.0;
    }
    return compareOot3dShadow2d(texelFetch(uOot3dShadow2d, uv, 0).x, z);
}

float mixOot3dShadow2d(vec4 samples, vec2 factor) {
    vec2 vertical = mix(samples.xy, samples.zw, factor.yy);
    return mix(vertical.x, vertical.y, factor.x);
}

vec3 sampleOot3dShadow2d(vec2 uv, float w) {
    if (draw.shadowOrthographic == 0) {
        uv /= w;
    }
    uint z = uint(max(0, int(min(abs(w), 1.0) * 16777215.0) -
                          draw.shadowTextureBias));
    ivec2 size = textureSize(uOot3dShadow2d, 0);
    vec2 coordinate = vec2(size) * uv - vec2(0.5);
    vec2 coordinateFloor = floor(coordinate);
    vec2 factor = coordinate - coordinateFloor;
    ivec2 base = ivec2(coordinateFloor);
    vec4 samples = vec4(
        sampleOot3dShadow2dTap(base, z),
        sampleOot3dShadow2dTap(base + ivec2(1, 0), z),
        sampleOot3dShadow2dTap(base + ivec2(0, 1), z),
        sampleOot3dShadow2dTap(base + ivec2(1, 1), z));
    float value = mixOot3dShadow2d(samples, factor);
    if (draw.shadowInvert != 0) {
        value = 1.0 - value;
    }
    return vec3(value);
}
)glsl";
    }
    source << R"glsl(

void main() {
    float drawRandom = (randomValue(vec3(floor(gl_FragCoord.xy * draw.noiseScale),
                                           float(draw.frameCount))) + 1.0) / 2.0;
)glsl";
    if (features.usedTextures[0]) {
        source << "    vec4 texVal0 = texture(uTex0, vTexCoord0);\n";
    }
    if (features.usedTextures[1]) {
        source << "    vec4 texVal1 = texture(uTex1, vTexCoord1);\n";
    }
    for (int input = 0; input < features.numInputs; ++input) {
        if (input == 0 && picaShadow2d) {
            if (features.opt_alpha) {
                source << "    vec4 shadowedInput1 = vec4(vInput1.rgb * "
                          "sampleOot3dShadow2d(vShadowCoordinate.xy, vShadowCoordinate.z), "
                          "vInput1.a);\n";
            } else {
                source << "    vec3 shadowedInput1 = vInput1 * "
                          "sampleOot3dShadow2d(vShadowCoordinate.xy, vShadowCoordinate.z);\n";
            }
            source << "    vec" << (features.opt_alpha ? 4 : 3)
                   << " input1 = picaByteRound(shadowedInput1);\n";
        } else {
            source << "    vec" << (features.opt_alpha ? 4 : 3) << " input" << (input + 1)
                   << " = " << (picaTextureEnv ? "picaByteRound(" : "") << "vInput"
                   << (input + 1) << (picaTextureEnv ? ")" : "") << ";\n";
        }
    }
    source << "    " << (features.opt_alpha ? "vec4" : "vec3") << " texel;\n";
    const int cycleCount = features.opt_2cyc ? 2 : 1;
    for (int cycle = 0; cycle < cycleCount; ++cycle) {
        if (cycle == 1) {
            if (features.opt_alpha) {
                if (picaTextureEnv) {
                    source << "    texel.a = picaByteRound(vec4(clamp(texel.a, 0.0, 1.0))).a;\n";
                } else {
                    const bool combined = features.c[cycle][1][2] == SHADER_COMBINED;
                    source << "    texel.a = mod(texel.a - (" << (combined ? "-1.01" : "-0.51")
                           << "), " << (combined ? "2.02" : "2.02") << ") + ("
                           << (combined ? "-1.01" : "-0.51") << ");\n";
                }
            }
            if (picaTextureEnv) {
                source << "    texel.rgb = picaByteRound(clamp(texel.rgb, 0.0, 1.0));\n";
            } else {
                const bool combined = features.c[cycle][0][2] == SHADER_COMBINED;
                source << "    texel.rgb = mod(texel.rgb - vec3("
                       << (combined ? "-1.01" : "-0.51") << "), vec3(2.02)) + vec3("
                       << (combined ? "-1.01" : "-0.51") << ");\n";
            }
        }
        if (features.opt_alpha && !features.color_alpha_same[cycle]) {
            source << "    texel = vec4(" << BuildCombinerFormula(features, cycle, false, false)
                   << ", " << BuildCombinerFormula(features, cycle, true, true) << ");\n";
        } else {
            source << "    texel = "
                   << BuildCombinerFormula(features, cycle, false, features.opt_alpha) << ";\n";
        }
    }
    if (picaTexture2) {
        source << "    texel.rgb = clamp(texel.rgb, 0.0, 1.0);\n"
                  "    texel.rgb = clamp(texture(uTex2, vTexCoord2).rgb * texVal1.rgb + "
                  "texel.rgb, 0.0, 1.0);\n";
    }
    if (picaPostMultiply) {
        source << "    texel.rgb *= input1.rgb;\n";
    }
    if (picaTextureEnv) {
        source << "    texel = picaByteRound(clamp(texel, 0.0, 1.0));\n";
    } else {
        source << "    texel = clamp(mod(texel - "
               << (features.opt_alpha ? "vec4(-0.51)" : "vec3(-0.51)") << ", "
               << (features.opt_alpha ? "vec4(2.02)" : "vec3(2.02)") << ") + "
               << (features.opt_alpha ? "vec4(-0.51)" : "vec3(-0.51)")
               << ", 0.0, 1.0);\n";
    }
    if (features.opt_fog) {
        if (picaFog) {
            source << "    float fogFactor = samplePicaFogFactor();\n";
            if (features.opt_alpha) {
                source << "    texel = vec4(mix(vFog.rgb, texel.rgb, fogFactor), texel.a);\n";
            } else {
                source << "    texel = mix(vFog.rgb, texel, fogFactor);\n";
            }
        } else if (features.opt_alpha) {
            source << "    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);\n";
        } else {
            source << "    texel = mix(texel, vFog.rgb, vFog.a);\n";
        }
    }
    if (features.opt_texture_edge && features.opt_alpha) {
        source << "    if (texel.a > 0.19) texel.a = 1.0; else discard;\n";
    }
    if (features.opt_alpha && features.opt_noise) {
        source << "    texel.a *= floor(clamp(randomValue(vec3(floor(gl_FragCoord.xy * "
                  "draw.noiseScale), float(draw.frameCount))) + texel.a, 0.0, 1.0));\n";
    }
    if (features.opt_grayscale) {
        source << "    float intensity = (texel.r + texel.g + texel.b) / 3.0;\n"
                  "    texel.rgb = mix(texel.rgb, vGrayscale.rgb * intensity, vGrayscale.a);\n";
    }
    if (features.opt_alpha && features.opt_alpha_threshold) {
        if (picaAlphaTest) {
            source << R"glsl(
    if (draw.alphaTestEnabled != 0) {
        int alphaU8 = int(clamp(texel.a, 0.0, 1.0) * 255.0);
        bool alphaPass = false;
        if (draw.alphaTestFunction == 1) alphaPass = true;
        else if (draw.alphaTestFunction == 2) alphaPass = alphaU8 == draw.alphaTestReference;
        else if (draw.alphaTestFunction == 3) alphaPass = alphaU8 != draw.alphaTestReference;
        else if (draw.alphaTestFunction == 4) alphaPass = alphaU8 < draw.alphaTestReference;
        else if (draw.alphaTestFunction == 5) alphaPass = alphaU8 <= draw.alphaTestReference;
        else if (draw.alphaTestFunction == 6) alphaPass = alphaU8 > draw.alphaTestReference;
        else if (draw.alphaTestFunction == 7) alphaPass = alphaU8 >= draw.alphaTestReference;
        if (!alphaPass) discard;
    } else if (texel.a < 8.0 / 256.0) {
        discard;
    }
)glsl";
        } else {
            source << "    if (texel.a < 8.0 / 256.0) discard;\n";
        }
    }
    if (features.opt_alpha && features.opt_invisible) {
        source << "    texel.a = 0.0;\n";
    }
    source << "    outColor = " << (features.opt_alpha ? "texel" : "vec4(texel, 1.0)")
           << ";\n";
    if (srgb) {
        source << "    outColor = fromLinear(outColor);\n";
    }
    if (features.opt_prim_depth) {
        source << "    gl_FragDepth = draw.primDepth;\n";
    }
    source << "}\n";
    return source.str();
}


}
