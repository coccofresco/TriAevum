#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"

#include "fast/backends/gfx_rendering_api.h"
#include "fast/interpreter.h"
#include "fast/legacy_gbi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr uint64_t kDefaultFast3dShaderMask = Fast::ShaderIdMask(Fast::SHADER_ID_OOT3D_NATIVE_DEFAULT);
constexpr uint64_t kPicaTextureEnvShaderMask = Fast::ShaderIdMask(Fast::SHADER_ID_PICA_TEXTURE_ENV);
constexpr uint64_t kPicaTextureEnvShadow2dShaderMask =
    Fast::ShaderIdMask(Fast::SHADER_ID_PICA_TEXTURE_ENV_SHADOW2D);
constexpr uint64_t kPicaTextureEnvPostMultiplyShaderMask =
    Fast::ShaderIdMask(Fast::SHADER_ID_PICA_TEXTURE_ENV_POST_MULTIPLY);
constexpr uint64_t kPicaFogShaderMask = Fast::ShaderIdMask(Fast::SHADER_ID_PICA_FOG);
constexpr uint64_t kPicaAlphaTestShaderMask =
    Fast::ShaderIdMask(Fast::SHADER_ID_PICA_ALPHA_TEST);
constexpr uint64_t kPicaTexture2MultAddShaderMask =
    Fast::ShaderIdMask(Fast::SHADER_ID_PICA_TEXTURE2_MULT_ADD);
constexpr uint32_t kOot3dShadow2dTextureUnit = 6;
constexpr size_t kPackedBatchCacheEntryLimit = 512;

struct ClipVertex {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
};

Matrix4f IdentityMatrix() {
    Matrix4f matrix{};
    for (size_t i = 0; i < 4; ++i) {
        matrix.M[i][i] = 1.0f;
    }
    return matrix;
}

Matrix4f MultiplyMatrices(const Matrix4f& left, const Matrix4f& right) {
    Matrix4f result{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t component = 0; component < 4; ++component) {
                result.M[row][column] += left.M[row][component] * right.M[component][column];
            }
        }
    }
    return result;
}

void AdjustMatrixForBackendClipParameters(Matrix4f& matrix, Fast::GfxClipParameters clipParameters) {
    if (clipParameters.z_is_from_0_to_1) {
        for (size_t column = 0; column < 4; ++column) {
            matrix.M[2][column] = (matrix.M[2][column] + matrix.M[3][column]) * 0.5f;
        }
    }
    if (clipParameters.invertY) {
        for (float& value : matrix.M[1]) {
            value = -value;
        }
    }
}

bool IsTextureUploadable(const Oot3dNativeRenderTexture& texture) {
    return texture.Rgba8Decoded && texture.Width > 0 && texture.Height > 0 &&
           texture.Rgba8.size() == static_cast<size_t>(texture.Width) * static_cast<size_t>(texture.Height) * 4;
}

std::optional<Fast::GfxNativeBlendFactor> NativeBlendFactorFromCmb(uint16_t value) {
    switch (value) {
        case 0x0000:
            return Fast::GfxNativeBlendFactor::Zero;
        case 0x0001:
            return Fast::GfxNativeBlendFactor::One;
        case 0x0300:
            return Fast::GfxNativeBlendFactor::SourceColor;
        case 0x0301:
            return Fast::GfxNativeBlendFactor::OneMinusSourceColor;
        case 0x0302:
            return Fast::GfxNativeBlendFactor::SourceAlpha;
        case 0x0303:
            return Fast::GfxNativeBlendFactor::OneMinusSourceAlpha;
        case 0x0304:
            return Fast::GfxNativeBlendFactor::DestAlpha;
        case 0x0305:
            return Fast::GfxNativeBlendFactor::OneMinusDestAlpha;
        case 0x0306:
            return Fast::GfxNativeBlendFactor::DestColor;
        case 0x0307:
            return Fast::GfxNativeBlendFactor::OneMinusDestColor;
        case 0x0308:
            return Fast::GfxNativeBlendFactor::SourceAlphaSaturate;
        case 0x8001:
            return Fast::GfxNativeBlendFactor::ConstantColor;
        case 0x8002:
            return Fast::GfxNativeBlendFactor::OneMinusConstantColor;
        case 0x8003:
            return Fast::GfxNativeBlendFactor::ConstantAlpha;
        case 0x8004:
            return Fast::GfxNativeBlendFactor::OneMinusConstantAlpha;
        default:
            return std::nullopt;
    }
}

std::optional<Fast::GfxNativeBlendEquation> NativeBlendEquationFromCmb(uint16_t value) {
    switch (value) {
        case 0x8006:
            return Fast::GfxNativeBlendEquation::Add;
        case 0x8007:
            return Fast::GfxNativeBlendEquation::Min;
        case 0x8008:
            return Fast::GfxNativeBlendEquation::Max;
        case 0x800A:
            return Fast::GfxNativeBlendEquation::Subtract;
        case 0x800B:
            return Fast::GfxNativeBlendEquation::ReverseSubtract;
        default:
            return std::nullopt;
    }
}

bool NativeBlendFactorUsesSourceAlpha(Fast::GfxNativeBlendFactor factor) {
    return factor == Fast::GfxNativeBlendFactor::SourceAlpha ||
           factor == Fast::GfxNativeBlendFactor::OneMinusSourceAlpha ||
           factor == Fast::GfxNativeBlendFactor::SourceAlphaSaturate;
}

bool NativeBlendStateNeedsSourceAlpha(const Fast::GfxNativeBlendState& state) {
    return NativeBlendFactorUsesSourceAlpha(state.SourceRgb) ||
           NativeBlendFactorUsesSourceAlpha(state.DestRgb) ||
           NativeBlendFactorUsesSourceAlpha(state.SourceAlpha) ||
           NativeBlendFactorUsesSourceAlpha(state.DestAlpha);
}

Fast::GfxNativeBlendState SourceAlphaOverBlendState() {
    Fast::GfxNativeBlendState state;
    state.Enabled = true;
    state.EquationRgb = Fast::GfxNativeBlendEquation::Add;
    state.EquationAlpha = Fast::GfxNativeBlendEquation::Add;
    state.SourceRgb = Fast::GfxNativeBlendFactor::SourceAlpha;
    state.DestRgb = Fast::GfxNativeBlendFactor::OneMinusSourceAlpha;
    state.SourceAlpha = Fast::GfxNativeBlendFactor::SourceAlpha;
    state.DestAlpha = Fast::GfxNativeBlendFactor::OneMinusSourceAlpha;
    return state;
}

std::optional<Fast::GfxNativeBlendState> BuildNativeMaterialBlendState(
    const Oot3dNativeRenderMaterialState& material) {
    if (!material.NativeBlendStateEnabled || !material.NativeBlendStateSupported) {
        return std::nullopt;
    }

    const auto srcRgb = NativeBlendFactorFromCmb(material.BlendSrc);
    const auto dstRgb = NativeBlendFactorFromCmb(material.BlendDst);
    const auto eqRgb = NativeBlendEquationFromCmb(material.BlendEquation);
    const auto srcAlpha = NativeBlendFactorFromCmb(material.ColorBlendSrc);
    const auto dstAlpha = NativeBlendFactorFromCmb(material.ColorBlendDst);
    const auto eqAlpha = NativeBlendEquationFromCmb(material.ColorBlendEquation);
    if (!srcRgb || !dstRgb || !eqRgb || !srcAlpha || !dstAlpha || !eqAlpha) {
        return std::nullopt;
    }

    Fast::GfxNativeBlendState state;
    state.Enabled = true;
    state.EquationRgb = *eqRgb;
    state.EquationAlpha = *eqAlpha;
    state.SourceRgb = *srcRgb;
    state.DestRgb = *dstRgb;
    state.SourceAlpha = *srcAlpha;
    state.DestAlpha = *dstAlpha;
    state.ConstantColor[3] = std::clamp(material.BlendColorAlpha, 0.0f, 1.0f);
    return state;
}

Fast::GfxNativeBlendState SourceAlphaAdditiveBlendState() {
    Fast::GfxNativeBlendState state;
    state.Enabled = true;
    state.EquationRgb = Fast::GfxNativeBlendEquation::Add;
    state.EquationAlpha = Fast::GfxNativeBlendEquation::Add;
    state.SourceRgb = Fast::GfxNativeBlendFactor::SourceAlpha;
    state.DestRgb = Fast::GfxNativeBlendFactor::One;
    state.SourceAlpha = Fast::GfxNativeBlendFactor::SourceAlpha;
    state.DestAlpha = Fast::GfxNativeBlendFactor::One;
    return state;
}

float NativeSmoothStepToF(float current, float target, float scale,
                          float maxStep, float minStep) {
    const float difference = target - current;
    if (difference == 0.0f) {
        return target;
    }
    float step = difference * std::max(scale, 0.0f);
    const float absoluteMaxStep = std::abs(maxStep);
    if (absoluteMaxStep > 0.0f && std::abs(step) > absoluteMaxStep) {
        step = std::copysign(absoluteMaxStep, step);
    }
    const float absoluteMinStep = std::abs(minStep);
    if (absoluteMinStep > 0.0f && std::abs(step) < absoluteMinStep) {
        step = std::copysign(absoluteMinStep, difference);
    }
    if (std::abs(step) >= std::abs(difference)) {
        return target;
    }
    return current + step;
}

uint32_t NativeSamplerWrapMode(uint16_t value) {
    switch (value) {
        case 0x812F:
            return G_TX_NOMIRROR | G_TX_CLAMP;
        case 0x8370:
            return G_TX_MIRROR | G_TX_WRAP;
        case 0x2901:
        default:
            return G_TX_NOMIRROR | G_TX_WRAP;
    }
}

uint64_t PackShaderInput(uint32_t value, uint32_t cycle, uint32_t component, uint32_t term) {
    return static_cast<uint64_t>(value) << (cycle * 32 + component * 16 + term * 4);
}

uint64_t BuildAlphaShaderId0(uint32_t alphaInput, bool textureAlphaMultipliesVertexAlpha) {
    if (textureAlphaMultipliesVertexAlpha) {
        return PackShaderInput(SHADER_TEXEL0, 0, 1, 0) |
               PackShaderInput(SHADER_0, 0, 1, 1) |
               PackShaderInput(SHADER_INPUT_1, 0, 1, 2) |
               PackShaderInput(SHADER_0, 0, 1, 3);
    }
    return PackShaderInput(alphaInput, 0, 1, 3);
}

uint64_t BuildDirectColorShaderId0(uint32_t colorInput, uint32_t alphaInput,
                                   bool textureAlphaMultipliesVertexAlpha) {
    return PackShaderInput(colorInput, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha);
}

uint64_t BuildTextureVertexColorShaderId0(uint32_t alphaInput,
                                          bool textureAlphaMultipliesVertexAlpha) {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_0, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha);
}

uint64_t BuildTextureVertexColorAddShaderId0(uint32_t alphaInput,
                                             bool textureAlphaMultipliesVertexAlpha,
                                             bool textureColorUsesTexture0Alpha) {
    const uint32_t textureColorInput =
        textureColorUsesTexture0Alpha ? SHADER_TEXEL0A : SHADER_TEXEL0;
    return PackShaderInput(textureColorInput, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_INPUT_2, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha);
}

uint64_t BuildTextureVertexColorMultiplyShaderId0(uint32_t alphaInput,
                                                  bool textureAlphaMultipliesVertexAlpha) {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_0, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_INPUT_2, 1, 0, 2) |
           PackShaderInput(SHADER_0, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

uint64_t BuildTextureVertexColorAddMultiplyShaderId0(uint32_t alphaInput,
                                                     bool textureAlphaMultipliesVertexAlpha,
                                                     bool textureColorUsesTexture0Alpha) {
    const uint32_t textureColorInput =
        textureColorUsesTexture0Alpha ? SHADER_TEXEL0A : SHADER_TEXEL0;
    return PackShaderInput(textureColorInput, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_INPUT_2, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_INPUT_3, 1, 0, 2) |
           PackShaderInput(SHADER_0, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

uint64_t BuildTexture0Texture1VertexColorAddShaderId0(
    uint32_t alphaInput, bool textureAlphaMultipliesVertexAlpha,
    bool stageLocalPrimaryColorScale) {
    const uint32_t stage1PrimaryColorInput =
        stageLocalPrimaryColorScale ? SHADER_INPUT_2 : SHADER_INPUT_1;
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_0, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha) |
           PackShaderInput(SHADER_TEXEL1, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(stage1PrimaryColorInput, 1, 0, 2) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

uint64_t BuildTexture0AlphaTexture1VertexColorAddShaderId0(
    uint32_t alphaInput, bool textureAlphaMultipliesVertexAlpha) {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 0, 2) |
           PackShaderInput(SHADER_0, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha) |
           PackShaderInput(SHADER_TEXEL0A, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_TEXEL1, 1, 0, 2) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

uint64_t BuildTexture0Texture1VertexColorMultiplyShaderId0() {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_TEXEL1, 0, 0, 2) |
           PackShaderInput(SHADER_0, 0, 0, 3) |
           PackShaderInput(SHADER_TEXEL0, 0, 1, 0) |
           PackShaderInput(SHADER_0, 0, 1, 1) |
           PackShaderInput(SHADER_TEXEL1, 0, 1, 2) |
           PackShaderInput(SHADER_0, 0, 1, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 1, 0, 2) |
           PackShaderInput(SHADER_0, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 0) |
           PackShaderInput(SHADER_0, 1, 1, 1) |
           PackShaderInput(SHADER_INPUT_1, 1, 1, 2) |
           PackShaderInput(SHADER_0, 1, 1, 3);
}

uint64_t BuildTexture0Texture1AddMultiplyTexture0ShaderId0() {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_1, 0, 0, 2) |
           PackShaderInput(SHADER_TEXEL1, 0, 0, 3) |
           PackShaderInput(SHADER_TEXEL0, 0, 1, 0) |
           PackShaderInput(SHADER_0, 0, 1, 1) |
           PackShaderInput(SHADER_INPUT_1, 0, 1, 2) |
           PackShaderInput(SHADER_0, 0, 1, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_TEXEL1, 1, 0, 2) |
           PackShaderInput(SHADER_0, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

uint64_t BuildTexture0Texture1AddThenVertexColorMultiplyShaderId0(
    uint32_t alphaInput, bool textureAlphaMultipliesVertexAlpha) {
    return PackShaderInput(SHADER_TEXEL0, 0, 0, 0) |
           PackShaderInput(SHADER_0, 0, 0, 1) |
           PackShaderInput(SHADER_1, 0, 0, 2) |
           PackShaderInput(SHADER_TEXEL1, 0, 0, 3) |
           BuildAlphaShaderId0(alphaInput, textureAlphaMultipliesVertexAlpha) |
           PackShaderInput(SHADER_COMBINED, 1, 0, 0) |
           PackShaderInput(SHADER_0, 1, 0, 1) |
           PackShaderInput(SHADER_INPUT_1, 1, 0, 2) |
           PackShaderInput(SHADER_0, 1, 0, 3) |
           PackShaderInput(SHADER_COMBINED, 1, 1, 3);
}

std::optional<uint32_t> NativePicaCompareFunctionFromCmb(uint16_t value) {
    switch (value) {
        case 0x0200:
            return 0; // Never
        case 0x0207:
            return 1; // Always
        case 0x0202:
            return 2; // Equal
        case 0x0205:
            return 3; // NotEqual
        case 0x0201:
            return 4; // LessThan
        case 0x0203:
            return 5; // LessThanOrEqual
        case 0x0204:
            return 6; // GreaterThan
        case 0x0206:
            return 7; // GreaterThanOrEqual
        default:
            return std::nullopt;
    }
}

std::optional<Fast::GfxNativeTextureFilter> NativeTextureFilter(uint16_t value) {
    switch (value) {
        case 0x2600:
            return Fast::GfxNativeTextureFilter::Nearest;
        case 0x2601:
            return Fast::GfxNativeTextureFilter::Linear;
        case 0x2700:
            return Fast::GfxNativeTextureFilter::NearestMipmapNearest;
        case 0x2701:
            return Fast::GfxNativeTextureFilter::LinearMipmapNearest;
        case 0x2702:
            return Fast::GfxNativeTextureFilter::NearestMipmapLinear;
        case 0x2703:
            return Fast::GfxNativeTextureFilter::LinearMipmapLinear;
        default:
            return std::nullopt;
    }
}

std::optional<Fast::GfxNativeTextureWrap> NativeTextureWrap(uint16_t value) {
    switch (value) {
        case 0x812F:
            return Fast::GfxNativeTextureWrap::ClampToEdge;
        case 0x8370:
            return Fast::GfxNativeTextureWrap::MirroredRepeat;
        case 0x2901:
            return Fast::GfxNativeTextureWrap::Repeat;
        default:
            return std::nullopt;
    }
}

uint64_t BuildShaderId1(bool alpha, bool alphaThreshold, bool picaTextureEnvClamp,
                        bool picaFog, bool picaAlphaTest,
                        bool picaShadow2dPrimaryRgb, bool picaTexture2MultAdd,
                        bool picaTextureEnvPostMultiply,
                        bool twoCycle) {
    uint64_t shaderId = kDefaultFast3dShaderMask;
    if (picaTexture2MultAdd) {
        shaderId = kPicaTexture2MultAddShaderMask;
    } else if (picaTextureEnvPostMultiply) {
        shaderId = kPicaTextureEnvPostMultiplyShaderMask;
    } else if (picaShadow2dPrimaryRgb) {
        shaderId = kPicaTextureEnvShadow2dShaderMask;
    } else if (picaTextureEnvClamp) {
        shaderId = kPicaTextureEnvShaderMask;
    } else if (picaFog) {
        shaderId = kPicaFogShaderMask;
    } else if (picaAlphaTest) {
        shaderId = kPicaAlphaTestShaderMask;
    }
    if (alpha) {
        shaderId |= SHADER_OPT(ALPHA);
    }
    if (alphaThreshold) {
        shaderId |= SHADER_OPT(ALPHA_THRESHOLD);
    }
    if (picaFog) {
        shaderId |= SHADER_OPT(FOG);
    }
    if (twoCycle) {
        shaderId |= SHADER_OPT(_2CYC);
    }
    return shaderId;
}

ClipVertex TransformPosition(const Matrix4f& transform, const ClipVertex& position) {
    return {
        transform.M[0][0] * position.X + transform.M[0][1] * position.Y + transform.M[0][2] * position.Z +
            transform.M[0][3] * position.W,
        transform.M[1][0] * position.X + transform.M[1][1] * position.Y + transform.M[1][2] * position.Z +
            transform.M[1][3] * position.W,
        transform.M[2][0] * position.X + transform.M[2][1] * position.Y + transform.M[2][2] * position.Z +
            transform.M[2][3] * position.W,
        transform.M[3][0] * position.X + transform.M[3][1] * position.Y + transform.M[3][2] * position.Z +
            transform.M[3][3] * position.W,
    };
}

ClipVertex TransformPosition(const Matrix4f& transform, const Vec3f& position) {
    return TransformPosition(transform, { position.X, position.Y, position.Z, 1.0f });
}

bool ModelBoundsOutsideClipVolume(const Oot3dNativeRenderModel& model,
                                  const Matrix4f& worldToClip,
                                  Fast::GfxClipParameters clipParameters,
                                  bool adjustForBackendClipParameters) {
    if (!model.LocalBounds.Valid) {
        return false;
    }

    Matrix4f modelToClip = MultiplyMatrices(worldToClip, model.ModelToWorld);
    if (adjustForBackendClipParameters) {
        AdjustMatrixForBackendClipParameters(modelToClip, clipParameters);
    }
    ClipVertex corners[8];
    size_t cornerIndex = 0;
    for (double x : { model.LocalBounds.Min.X, model.LocalBounds.Max.X }) {
        for (double y : { model.LocalBounds.Min.Y, model.LocalBounds.Max.Y }) {
            for (double z : { model.LocalBounds.Min.Z, model.LocalBounds.Max.Z }) {
                const Vec3f corner = {
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                };
                corners[cornerIndex++] = TransformPosition(modelToClip, corner);
            }
        }
    }

    for (const auto& corner : corners) {
        if (!std::isfinite(corner.X) || !std::isfinite(corner.Y) ||
            !std::isfinite(corner.Z) || !std::isfinite(corner.W)) {
            return false;
        }
    }

    const auto allOutside = [&](const auto& planeDistance) {
        return std::all_of(std::begin(corners), std::end(corners),
                           [&](const ClipVertex& corner) {
                               return planeDistance(corner) < 0.0f;
                           });
    };
    return allOutside([](const ClipVertex& corner) { return corner.X + corner.W; }) ||
           allOutside([](const ClipVertex& corner) { return corner.W - corner.X; }) ||
           allOutside([](const ClipVertex& corner) { return corner.Y + corner.W; }) ||
           allOutside([](const ClipVertex& corner) { return corner.W - corner.Y; }) ||
           (clipParameters.z_is_from_0_to_1
                ? allOutside([](const ClipVertex& corner) { return corner.Z; })
                : allOutside([](const ClipVertex& corner) { return corner.Z + corner.W; })) ||
           allOutside([](const ClipVertex& corner) { return corner.W - corner.Z; });
}

float ColorChannel(uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

float ColorAlpha(uint8_t value) {
    return ColorChannel(value);
}

bool InvertMatrix4f(const Matrix4f& matrix, Matrix4f& inverse) {
    double aug[4][8] = {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            aug[row][col] = matrix.M[row][col];
        }
        aug[row][row + 4] = 1.0;
    }

    for (size_t col = 0; col < 4; ++col) {
        size_t pivot = col;
        double pivotAbs = std::fabs(aug[pivot][col]);
        for (size_t row = col + 1; row < 4; ++row) {
            const double candidateAbs = std::fabs(aug[row][col]);
            if (candidateAbs > pivotAbs) {
                pivot = row;
                pivotAbs = candidateAbs;
            }
        }
        if (pivotAbs <= std::numeric_limits<double>::epsilon()) {
            return false;
        }
        if (pivot != col) {
            for (size_t i = 0; i < 8; ++i) {
                std::swap(aug[col][i], aug[pivot][i]);
            }
        }

        const double pivotValue = aug[col][col];
        for (size_t i = 0; i < 8; ++i) {
            aug[col][i] /= pivotValue;
        }
        for (size_t row = 0; row < 4; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = aug[row][col];
            for (size_t i = 0; i < 8; ++i) {
                aug[row][i] -= factor * aug[col][i];
            }
        }
    }

    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            inverse.M[row][col] = static_cast<float>(aug[row][col + 4]);
        }
    }
    return true;
}

bool BuildNativePicaFogLutForProjection(Oot3dNativePicaFogState& fog, const Matrix4f& viewToClip) {
    if (!fog.Available || !fog.FogEnabled || !(fog.SourceFar > fog.SourceNear) ||
        fog.SourceCurveMode != 0) {
        fog.LutWordCount = 0;
        return false;
    }

    Matrix4f picaProjection = viewToClip;
    // OOT3D's PICA projection maps near..far to 0..-1. The host projection maps it to -1..1.
    for (size_t col = 0; col < 4; ++col) {
        picaProjection.M[2][col] =
            -0.5f * (viewToClip.M[2][col] + viewToClip.M[3][col]);
    }

    Matrix4f clipToView{};
    if (!InvertMatrix4f(picaProjection, clipToView)) {
        fog.LutWordCount = 0;
        fog.BlockedReason = "pica_fog_projection_inverse_failed";
        return false;
    }

    constexpr size_t sampleCount = 129;
    constexpr float sampleStep = -1.0f / 128.0f;
    std::array<float, sampleCount> curve{};
    const float nearZ = static_cast<float>(static_cast<int32_t>(fog.SourceNear));
    const float farZ = static_cast<float>(static_cast<int32_t>(fog.SourceFar));
    if (!(farZ > nearZ)) {
        fog.LutWordCount = 0;
        return false;
    }
    for (size_t index = 0; index < curve.size(); ++index) {
        const float depth = static_cast<float>(index) * sampleStep;
        const float denominator =
            clipToView.M[3][3] + clipToView.M[3][2] * depth;
        if (denominator == 0.0f || !std::isfinite(denominator)) {
            fog.LutWordCount = 0;
            fog.BlockedReason = "pica_fog_projection_depth_failed";
            return false;
        }
        const float viewZ =
            -((clipToView.M[2][3] + clipToView.M[2][2] * depth) /
              denominator);
        curve[index] =
            viewZ < nearZ ? 1.0f
                          : (viewZ > farZ ? 0.0f
                                         : (farZ - viewZ) / (farZ - nearZ));
    }

    const auto packBase = [](float value) {
        if (!(value > 0.0f) || !std::isfinite(value)) {
            return uint32_t{0};
        }
        return static_cast<uint32_t>(
            std::min(value * 2048.0f, 2047.0f));
    };
    const auto packSlope = [](float value) {
        if (value == 0.0f || !std::isfinite(value)) {
            return uint32_t{0};
        }
        const float encoded =
            std::clamp((value + 2.0f) * 2048.0f, 0.0f, 8191.0f);
        return static_cast<uint32_t>(
            encoded < 4096.0f ? encoded + 4096.0f : encoded - 4096.0f);
    };
    for (size_t index = 0; index < fog.LutWords.size(); ++index) {
        if (fog.SourceRebuildGate != 0) {
            fog.LutWords[index] = 0;
            continue;
        }
        const float slope = curve[index + 1] - curve[index];
        fog.LutWords[index] =
            packSlope(slope) | (packBase(curve[index]) << 13);
    }
    fog.LutWordCount = static_cast<uint32_t>(fog.LutWords.size());
    return true;
}

uint64_t BuildNativePicaFogLutStateKey(const Oot3dNativePicaFogState& fog) {
    uint64_t hash = 1469598103934665603ULL;
    const auto appendWord = [&hash](uint32_t word) {
        hash ^= word;
        hash *= 1099511628211ULL;
    };
    appendWord(fog.LutWordCount);
    appendWord(fog.FogFlip ? 1u : 0u);
    for (size_t i = 0; i < fog.LutWordCount; ++i) {
        appendWord(fog.LutWords[i]);
    }
    return hash;
}

size_t VertexStride(bool textured, bool secondaryTextureCoordInput, bool tertiaryTextureCoordInput,
                    bool alpha, bool vertexColorInput) {
    size_t stride = 4;
    if (textured) {
        stride += 2;
    }
    if (secondaryTextureCoordInput) {
        stride += 2;
    }
    if (tertiaryTextureCoordInput) {
        stride += 2;
    }
    if (vertexColorInput) {
        stride += alpha ? 4 : 3;
    }
    return stride;
}

size_t VertexStride(bool textured, bool secondaryTextureCoordInput, bool tertiaryTextureCoordInput,
                    bool alpha, bool vertexColorInput,
                    bool textureColorAddendInput, bool textureColorMultiplierInput,
                    bool texture1ColorAddStageLocalPrimaryInput,
                    bool picaFog, bool picaShadow2dPrimaryRgb) {
    size_t stride = VertexStride(textured, secondaryTextureCoordInput, tertiaryTextureCoordInput,
                                 alpha, vertexColorInput);
    if (picaFog) {
        stride += 4;
    }
    if (textureColorAddendInput) {
        stride += alpha ? 4 : 3;
    }
    if (textureColorMultiplierInput) {
        stride += alpha ? 4 : 3;
    }
    if (texture1ColorAddStageLocalPrimaryInput) {
        stride += alpha ? 4 : 3;
    }
    if (picaShadow2dPrimaryRgb) {
        stride += 3;
    }
    return stride;
}

bool ColorRgbIsZero(ColorRgba8 color) {
    return color.R == 0 && color.G == 0 && color.B == 0;
}

bool Vec3IsIdentity(Vec3f value) {
    return value.X == 1.0f && value.Y == 1.0f && value.Z == 1.0f;
}

bool MaterialUsesTextureColorAddendShader(const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.TextureColorAddendResolved &&
           !ColorRgbIsZero(material.TextureEnvProgram.TextureColorAddend);
}

bool MaterialUsesTextureColorMultiplierShader(const Oot3dNativeRenderMaterialState& material) {
    const auto& multiplier = material.TextureEnvProgram.TextureColorMultiplier;
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.TextureColorMultiplierResolved &&
           (multiplier.X != 1.0f || multiplier.Y != 1.0f || multiplier.Z != 1.0f);
}

bool MaterialUsesTexture1ColorAddShader(const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.Texture1ColorAddResolved &&
           material.SecondaryTextureIndex >= 0;
}

bool MaterialUsesTexture1ColorMultiplyShader(const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.Texture1ColorMultiplyResolved &&
           material.SecondaryTextureIndex >= 0;
}

bool MaterialUsesTexture0Texture1AddThenPrimaryColorModulateShader(
    const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateResolved &&
           material.SecondaryTextureIndex >= 0;
}

bool MaterialUsesTexture1Texture2MultiplyAddPreviousShader(
    const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousResolved &&
           material.SecondaryTextureIndex >= 0 &&
           material.TertiaryTextureIndex >= 0;
}

bool MaterialUsesTexture0Texture1AddMultiplyTexture0Shader(
    const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.ColorShaderPathApplied &&
           material.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0Resolved &&
           material.SecondaryTextureIndex >= 0;
}

bool MaterialUsesNativePicaSelfShadowCandidate(const Oot3dNativePicaShadowState& shadow,
                                               const Oot3dNativeRenderMaterialState& material) {
    return shadow.Available && material.NativePicaSelfShadowCandidate;
}

bool BatchHasNativePicaShadow2dTexCoord0W(const Oot3dNativeRenderBatch& batch) {
    return !batch.Vertices.empty() &&
           std::all_of(batch.Vertices.begin(), batch.Vertices.end(), [](const Oot3dNativeRenderVertex& vertex) {
               return vertex.NativePicaShadow2dTexCoord0WAvailable;
           });
}

uint64_t HashTextureBytes(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void HashBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

template <typename T>
void HashValue(uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(value));
}

void HashBool(uint64_t& hash, bool value) {
    const uint8_t byte = value ? 1 : 0;
    HashValue(hash, byte);
}

void HashString(uint64_t& hash, const std::string& value) {
    HashValue(hash, value.size());
    HashBytes(hash, value.data(), value.size());
}

void HashColor(uint64_t& hash, const ColorRgba8& color) {
    HashValue(hash, color.R);
    HashValue(hash, color.G);
    HashValue(hash, color.B);
    HashValue(hash, color.A);
}

void HashVector(uint64_t& hash, const Vec3f& vector) {
    HashValue(hash, vector.X);
    HashValue(hash, vector.Y);
    HashValue(hash, vector.Z);
}

void HashMatrix(uint64_t& hash, const Matrix4f& matrix) {
    for (const auto& row : matrix.M) {
        for (const float value : row) {
            HashValue(hash, value);
        }
    }
}

uint64_t BuildRuntimePicaLightingStateKey(
    const std::optional<Oot3dNativePicaLightingRenderState>& lighting) {
    if (!lighting.has_value()) {
        return 0;
    }

    uint64_t hash = 1469598103934665603ull;
    HashBool(hash, lighting->Available);
    HashString(hash, lighting->VertexColorFormula);
    HashString(hash, lighting->DirectionalFormula);
    HashString(hash, lighting->TexturedBaseColorSource);
    HashString(hash, lighting->MaterialVertexHemisphereModelScope);
    HashString(hash, lighting->VertexHemisphereRuntimeColorSource);
    HashString(hash, lighting->VertexHemisphereRuntimeVectorSource);
    HashColor(hash, lighting->AmbientColor);
    HashColor(hash, lighting->DiffuseColor);
    HashColor(hash, lighting->Light1Color);
    HashColor(hash, lighting->VertexModulationColor);
    HashVector(hash, lighting->Light0Vector);
    HashVector(hash, lighting->Light1Vector);
    HashBool(hash, lighting->DirectionalVectorsUseModelSpace);
    HashBool(hash, lighting->VertexHemisphereUniformTraceDecoded);
    HashBool(hash, lighting->VertexHemisphereUniformTraceUsedAsRuntimeSource);
    HashColor(hash, lighting->VertexHemisphereAmbientColor);
    HashColor(hash, lighting->VertexHemisphereDiffuseColor);
    HashColor(hash, lighting->VertexHemisphereSecondaryColor);
    HashVector(hash, lighting->VertexHemisphereLightVector);
    HashVector(hash, lighting->VertexHemisphereNegatedLightVector);
    HashBool(hash, lighting->VertexHemisphereWorldLightVectorDecoded);
    HashVector(hash, lighting->VertexHemisphereWorldLightVector);
    HashVector(hash, lighting->VertexHemisphereWorldNegatedLightVector);
    HashBool(hash, lighting->ActorVsLightPacket.Available);
    HashBool(hash, lighting->ActorVsLightPacket.ColorPacketAvailable);
    HashBool(hash, lighting->ActorVsLightPacket.VectorLayoutResolved);
    HashBool(hash, lighting->ActorVsLightPacket.CompactPayloadSourceResolved);
    HashColor(hash, lighting->ActorVsLightPacket.AmbientColor);
    HashColor(hash, lighting->ActorVsLightPacket.Diffuse0Color);
    HashColor(hash, lighting->ActorVsLightPacket.Diffuse1Color);
    HashVector(hash, lighting->ActorVsLightPacket.CompactPayloadSlot0Direction);
    HashVector(hash, lighting->ActorVsLightPacket.CompactPayloadSlot1Direction);
    HashBool(hash, lighting->CmbVShaderLightingAccumulatorDecoded);
    HashValue(hash, lighting->CmbVShaderLightingAccumulatorSlotCount);
    HashValue(hash, lighting->DirectionalLightCount);
    HashBool(hash, lighting->ModulateTexturedBatches);
    HashBool(hash, lighting->PreserveVertexAlpha);
    return hash;
}

uint64_t BuildPackedVertexStateKey(uint64_t geometryContentVersion,
                                   uint64_t lightingStateKey, bool evaluateRuntimeLighting,
                                   bool flipTextureV, size_t maxTrianglesPerDraw,
                                   bool textured, bool secondaryTextureCoordInput,
                                   bool tertiaryTextureCoordInput, bool alpha,
                                   bool vertexInput, bool textureColorAddendInput,
                                   bool textureColorMultiplierInput,
                                   bool texture1ColorAddStageLocalPrimaryInput,
                                   bool picaFog, const ColorRgba8& fogColor,
                                   bool picaShadow2dPrimaryRgb,
                                   const Matrix4f& modelToWorld) {
    uint64_t hash = 1469598103934665603ull;
    HashValue(hash, geometryContentVersion);
    HashValue(hash, lightingStateKey);
    HashBool(hash, evaluateRuntimeLighting);
    HashBool(hash, flipTextureV);
    HashValue(hash, maxTrianglesPerDraw);
    HashBool(hash, textured);
    HashBool(hash, secondaryTextureCoordInput);
    HashBool(hash, tertiaryTextureCoordInput);
    HashBool(hash, alpha);
    HashBool(hash, vertexInput);
    HashBool(hash, textureColorAddendInput);
    HashBool(hash, textureColorMultiplierInput);
    HashBool(hash, texture1ColorAddStageLocalPrimaryInput);
    HashBool(hash, picaFog);
    if (picaFog) {
        HashColor(hash, fogColor);
    }
    HashBool(hash, picaShadow2dPrimaryRgb);
    if (evaluateRuntimeLighting) {
        HashMatrix(hash, modelToWorld);
    }
    return hash;
}

uint64_t BuildPackedVertexCacheId(uint64_t geometryId, uint32_t batchIndex,
                                  uint32_t chunkIndex) {
    uint64_t hash = 1469598103934665603ull;
    HashValue(hash, geometryId);
    HashValue(hash, batchIndex);
    HashValue(hash, chunkIndex);
    return hash != 0 ? hash : 1;
}

} // namespace

Oot3dNativeFast3dRenderConfig::Oot3dNativeFast3dRenderConfig()
    : WorldToClip(IdentityMatrix()), ViewToClip(IdentityMatrix()) {
}

bool Oot3dNativeFast3dRenderBackend::TextureCacheKey::operator<(const TextureCacheKey& other) const {
    return std::tie(ModelName, TextureName, TextureIndex, SourceIndex, Width, Height, TextureFormat, MipmapCount,
                    HasNativeAlpha, Rgba8ByteCount, Rgba8Hash) <
           std::tie(other.ModelName, other.TextureName, other.TextureIndex, other.SourceIndex, other.Width,
                    other.Height, other.TextureFormat, other.MipmapCount, other.HasNativeAlpha, other.Rgba8ByteCount,
                    other.Rgba8Hash);
}

bool Oot3dNativeFast3dRenderBackend::ShaderKey::operator<(const ShaderKey& other) const {
    return std::tie(Textured, Alpha, AlphaThreshold, PicaAlphaTest, VertexColorInput, VertexAlphaInput,
                    TextureAlphaMultipliesVertexAlpha,
                    PicaTextureEnvClamp, PicaFog, PicaShadow2dPrimaryRgb,
                    TextureColorAddendInput, TextureColorAddTexture0AlphaInput,
                    TextureColorMultiplierInput,
                    Texture1ColorAddInput, Texture1ColorAddStageLocalPrimaryInput,
                    Texture1ColorAddTexture0AlphaInput,
                    Texture1ColorMultiplyInput,
                    Texture0Texture1AddThenPrimaryColorModulateInput,
                    Texture1Texture2MultiplyAddPreviousInput,
                    Texture0Texture1AddMultiplyTexture0Input) <
           std::tie(other.Textured, other.Alpha, other.AlphaThreshold, other.PicaAlphaTest, other.VertexColorInput,
                    other.VertexAlphaInput,
                    other.TextureAlphaMultipliesVertexAlpha,
                    other.PicaTextureEnvClamp, other.PicaFog, other.PicaShadow2dPrimaryRgb,
                    other.TextureColorAddendInput,
                    other.TextureColorAddTexture0AlphaInput,
                    other.TextureColorMultiplierInput,
                    other.Texture1ColorAddInput, other.Texture1ColorAddStageLocalPrimaryInput,
                    other.Texture1ColorAddTexture0AlphaInput,
                    other.Texture1ColorMultiplyInput,
                    other.Texture0Texture1AddThenPrimaryColorModulateInput,
                    other.Texture1Texture2MultiplyAddPreviousInput,
                    other.Texture0Texture1AddMultiplyTexture0Input);
}

Oot3dNativeFast3dRenderBackend::Oot3dNativeFast3dRenderBackend(Fast::GfxRenderingAPI& renderingApi,
                                                               Oot3dNativeFast3dRenderConfig config)
    : mRenderingApi(renderingApi), mConfig(std::move(config)) {
}

Oot3dNativeFast3dRenderBackend::~Oot3dNativeFast3dRenderBackend() {
    if (mCurrentShader != nullptr) {
        mRenderingApi.UnloadShader(mCurrentShader);
        mCurrentShader = nullptr;
    }
    ReleaseTextures();
}

void Oot3dNativeFast3dRenderBackend::BeginScene(const Oot3dNativeDemoRenderScene& scene) {
    mPicaLighting = mRuntimePicaLighting.value_or(scene.PicaLighting);
    mPicaShadow = scene.PicaShadow;
    mPicaFog = mRuntimePicaFog.value_or(scene.PicaFog);
    const bool picaFogLutBuilt =
        mConfig.ViewToClipAvailable && BuildNativePicaFogLutForProjection(mPicaFog, mConfig.ViewToClip);
    mPicaFogFragmentLutBackendSupported = mRenderingApi.SupportsOot3dPicaFogLut();
    mPicaFogLutStateKey = picaFogLutBuilt ? BuildNativePicaFogLutStateKey(mPicaFog) : 0;
    mPicaFog.UsedForRender = picaFogLutBuilt && mPicaFog.ShaderSupported &&
                             mPicaFog.LutShaderSupported && mPicaFogFragmentLutBackendSupported;
    mStats.NativePicaFogAvailable = mPicaFog.Available;
    mStats.NativePicaFogUsedForRender = mPicaFog.UsedForRender;
    mStats.NativePicaFogShaderSupported = mPicaFog.ShaderSupported;
    mStats.NativePicaFogLutShaderSupported = mPicaFog.LutShaderSupported && picaFogLutBuilt &&
                                              mPicaFogFragmentLutBackendSupported;
    mStats.NativePicaFogFragmentLutBackendSupported = mPicaFogFragmentLutBackendSupported;
    mStats.NativePicaFogEnabled = mPicaFog.FogEnabled;
    mStats.NativePicaFogSourceKind = mPicaFog.SourceKind;
    mStats.NativePicaFogBlockedReason = mPicaFog.BlockedReason;
    if (mPicaFog.Available && mPicaFog.FogEnabled && !mConfig.ViewToClipAvailable) {
        mStats.NativePicaFogBlockedReason = "native_projection_matrix_not_provided_to_backend";
    }
    if (mPicaFog.Available && mPicaFog.FogEnabled && !picaFogLutBuilt &&
        mStats.NativePicaFogBlockedReason.empty()) {
        mStats.NativePicaFogBlockedReason = "native_pica_fog_lut_not_built_for_current_scene";
    }
    if (picaFogLutBuilt && !mPicaFogFragmentLutBackendSupported &&
        mStats.NativePicaFogBlockedReason.empty()) {
        mStats.NativePicaFogBlockedReason = "native_pica_fog_fragment_lut_backend_not_supported";
    }
    mStats.NativePicaFogColor = mPicaFog.Color;
    mStats.NativePicaFogMode = mPicaFog.Mode;
    mStats.NativePicaFogLutWordCount = mPicaFog.LutWordCount;
    mStats.NativePicaFogLutWords.assign(mPicaFog.LutWords.begin(),
                                        mPicaFog.LutWords.begin() + mPicaFog.LutWordCount);
    mStats.NativePicaSelfShadowRouteAvailable =
        scene.PicaShadow.Available &&
        !scene.PicaShadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm;
    mStats.NativePicaSelfShadowShaderRouteDecoded = scene.PicaShadow.SelfShadowShaderRouteDecoded;
    mStats.NativePicaSelfShadowShaderRoutePending =
        scene.PicaShadow.Available && !scene.PicaShadow.SelfShadowShaderRouteDecoded &&
        !scene.PicaShadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm;
    mStats.NativePicaSelfShadowShaderEquationSemanticsSupported =
        scene.PicaShadow.ShaderEquationSemanticsSupported;
    mStats.NativePicaSelfShadowTextureSamplingSemanticsSupported =
        scene.PicaShadow.ShadowTextureSamplingSemanticsSupported;
    mStats.NativePicaSelfShadowFullPrimaryLightContributionSupported =
        scene.PicaShadow.FullPrimaryLightContributionShadowSupported;
    mStats.NativePicaShadow2dTextureTypeSupported =
        scene.PicaShadow.Shadow2dTextureTypeSupported;
    mStats.NativePicaShadow2dBackendPassRequestSupported =
        scene.PicaShadow.Shadow2dBackendPassRequestSupported;
    mStats.NativePicaShadow2dVisualPassRequestSupported =
        scene.PicaShadow.Shadow2dVisualPassRequestSupported;
    mStats.NativePicaShadow2dMaterialTextureProjectionInputSupported =
        scene.PicaShadow.Shadow2dMaterialTextureProjectionInputSupported;
    mStats.NativePicaShadow2dTexCoord0WInputSupported =
        scene.PicaShadow.Shadow2dTexCoord0WInputSupported;
    mStats.NativePicaShadow2dEncodedDepthCompareSupported =
        scene.PicaShadow.Shadow2dEncodedDepthCompareSupported;
    mStats.NativePicaShadow2dProjectionRegisterValuesDecoded =
        scene.PicaShadow.Shadow2dProjectionRegisterValuesDecoded;
    mStats.NativePicaShadow2dProjectionRegisterTraceAvailable =
        scene.PicaShadow.Shadow2dProjectionRegisterTraceAvailable;
    mStats.NativePicaShadow2dDmpShadowZUniformsDecoded =
        scene.PicaShadow.Shadow2dDmpShadowZUniformsDecoded;
    mStats.NativePicaShadow2dPicaTextureShadowRegisterDecoded =
        scene.PicaShadow.Shadow2dPicaTextureShadowRegisterDecoded;
    mStats.NativePicaShadow2dPicaFramebufferShadowRegisterDecoded =
        scene.PicaShadow.Shadow2dPicaFramebufferShadowRegisterDecoded;
    mStats.NativePicaShadow2dShaderRouteRegisterTraceDecoded =
        scene.PicaShadow.Shadow2dShaderRouteRegisterTraceDecoded;
    mStats.NativePicaShadow2dShaderRouteMatchesPrimaryRgbShadowTerm =
        scene.PicaShadow.Shadow2dShaderRouteMatchesPrimaryRgbShadowTerm;
    mStats.NativePicaShadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm =
        scene.PicaShadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm;
    mStats.NativePicaShadow2dShadowTextureDimDecoded =
        scene.PicaShadow.Shadow2dShadowTextureDimDecoded;
    mStats.NativePicaShadow2dBackendShadowMapRenderTargetDecoded =
        scene.PicaShadow.Shadow2dBackendShadowMapRenderTargetDecoded;
    mStats.NativePicaShadow2dVisualPassReady =
        scene.PicaShadow.Shadow2dVisualPassReady;
    mStats.NativePicaShadow2dVisualPassUsesRuntimeN64AssetSubstitution =
        scene.PicaShadow.Shadow2dVisualPassUsesRuntimeN64AssetSubstitution;
    mStats.NativePicaSelfShadowUsesRuntimeN64AssetSubstitution =
        scene.PicaShadow.UsesRuntimeN64AssetSubstitution;
    mStats.NativePicaSelfShadowLightContributionFormula =
        scene.PicaShadow.ShadowLightContributionFormula;
    mStats.NativePicaShadow2dBackendPassRequestSource =
        scene.PicaShadow.Shadow2dBackendPassRequestSource;
    mStats.NativePicaShadow2dVisualPassSourceKind =
        scene.PicaShadow.Shadow2dVisualPassSourceKind;
    mStats.NativePicaShadow2dVisualPassRenderTargetFormat =
        scene.PicaShadow.Shadow2dVisualPassRenderTargetFormat;
    mStats.NativePicaShadow2dMaterialTextureProjectionInputSource =
        scene.PicaShadow.Shadow2dMaterialTextureProjectionInputSource;
    mStats.NativePicaShadow2dTexCoord0WInputSource =
        scene.PicaShadow.Shadow2dTexCoord0WInputSource;
    mStats.NativePicaShadow2dProjectionRegisterValueSource =
        scene.PicaShadow.Shadow2dProjectionRegisterValueSource;
    mStats.NativePicaShadow2dProjectionRegisterTraceSourceKind =
        scene.PicaShadow.Shadow2dProjectionRegisterTraceSourceKind;
    mStats.NativePicaShadow2dProjectionRegisterTraceFormat =
        scene.PicaShadow.Shadow2dProjectionRegisterTraceFormat;
    mStats.NativePicaShadow2dProjectionRegisterDecodeSource =
        scene.PicaShadow.Shadow2dProjectionRegisterDecodeSource;
    mStats.NativePicaShadow2dShaderRouteTraceStatus =
        scene.PicaShadow.Shadow2dShaderRouteTraceStatus;
    mStats.NativePicaShadow2dBackendShadowMapRenderTargetSource =
        scene.PicaShadow.Shadow2dBackendShadowMapRenderTargetSource;
    mStats.NativePicaShadow2dVisualPassApplication =
        scene.PicaShadow.Shadow2dVisualPassApplication;
    mStats.NativePicaShadow2dVisualPassBlockedReason =
        scene.PicaShadow.Shadow2dVisualPassBlockedReason;
    mStats.NativePicaShadow2dShadowMapFormat =
        scene.PicaShadow.ShadowMapFormat;
    mStats.NativePicaShadow2dCompareSource =
        scene.PicaShadow.ShadowTextureCompareSource;
    mStats.NativePicaShadow2dFilter =
        scene.PicaShadow.ShadowTextureFilter;
    mStats.NativePicaShadow2dZFormula =
        scene.PicaShadow.ShadowTextureZFormula;
    mStats.NativePicaShadow2dEncodedDepthDecodeSource =
        scene.PicaShadow.Shadow2dEncodedDepthDecodeSource;
    mStats.NativePicaShadow2dEncodedDepthBits =
        scene.PicaShadow.Shadow2dEncodedDepthBits;
    mStats.NativePicaShadow2dEncodedAlphaBits =
        scene.PicaShadow.Shadow2dEncodedAlphaBits;
    mStats.NativePicaShadow2dBiasShift =
        scene.PicaShadow.Shadow2dBiasShift;
    mStats.NativePicaShadow2dFilterTapCount =
        scene.PicaShadow.Shadow2dFilterTapCount;
    mStats.NativePicaShadow2dFilterResultChannelCount =
        scene.PicaShadow.Shadow2dFilterResultChannelCount;
    mStats.NativePicaShadow2dTextureShadowCompareBias =
        scene.PicaShadow.Shadow2dTextureShadowCompareBias;
    mStats.NativePicaShadow2dShadowTextureDimRegisterIndex =
        scene.PicaShadow.Shadow2dShadowTextureDimRegisterIndex;
    mStats.NativePicaShadow2dShadowTextureDimRaw =
        scene.PicaShadow.Shadow2dShadowTextureDimRaw;
    mStats.NativePicaShadow2dShadowTextureWidth =
        scene.PicaShadow.Shadow2dShadowTextureWidth;
    mStats.NativePicaShadow2dShadowTextureHeight =
        scene.PicaShadow.Shadow2dShadowTextureHeight;
    mStats.NativePicaShadow2dTextureShadowOrthographic =
        scene.PicaShadow.Shadow2dTextureShadowOrthographic;
    mStats.NativePicaShadow2dDmpShadowZBias =
        scene.PicaShadow.Shadow2dDmpShadowZBias;
    mStats.NativePicaShadow2dDmpShadowZScale =
        scene.PicaShadow.Shadow2dDmpShadowZScale;
    mStats.NativePicaShadow2dFramebufferShadowConstant =
        scene.PicaShadow.Shadow2dFramebufferShadowConstant;
    mStats.NativePicaShadow2dFramebufferShadowLinear =
        scene.PicaShadow.Shadow2dFramebufferShadowLinear;
    mStats.NativePicaShadow2dOutOfBoundsResult =
        scene.PicaShadow.Shadow2dOutOfBoundsResult;
    mStats.NativePicaShadow2dFilterInterpolationSource =
        scene.PicaShadow.Shadow2dFilterInterpolationSource;
    mStats.NativePicaSelfShadowedLightRegisterCount =
        scene.PicaShadow.FragmentLightShadowedRegisterCount;

    mStats.NativePicaShadow2dBackendShadowMapRenderTargetRequested =
        scene.PicaShadow.Shadow2dBackendPassRequestSupported &&
        scene.PicaShadow.Shadow2dProjectionRegisterValuesDecoded &&
        scene.PicaShadow.SelfShadowShaderRouteDecoded &&
        scene.PicaShadow.Shadow2dBackendShadowMapRenderTargetDecoded &&
        scene.PicaShadow.CandidateBatchCount > 0;
    mStats.NativePicaShadow2dBackendShadowMapRenderTargetAllocated = false;
    mStats.NativePicaShadow2dBackendShadowMapRenderTargetNativeFormatSupported = false;
    mStats.NativePicaShadow2dBackendR32uiPipelineSupported =
        mRenderingApi.SupportsOot3dShadow2dR32uiPipeline();
    mStats.NativePicaShadow2dBackendDepthEncodeShaderSupported =
        mStats.NativePicaShadow2dBackendR32uiPipelineSupported;
    mStats.NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported =
        mStats.NativePicaShadow2dBackendR32uiPipelineSupported;
    if (mStats.NativePicaShadow2dBackendDepthEncodeShaderSupported &&
        mStats.NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported &&
        mStats.NativePicaShadow2dVisualPassBlockedReason ==
            "shadow2d_backend_shadow_map_depth_encode_shader_not_implemented") {
        mStats.NativePicaShadow2dVisualPassBlockedReason =
            "shadow2d_backend_depth_encode_and_primary_rgb_compare_ready_pending_native_shadow_projection_submit_pass";
    }
    mStats.NativePicaShadow2dBackendShadowMapFramebufferId = -1;
    mStats.NativePicaShadow2dBackendShadowMapRenderTargetBlockedReason.clear();

    if (mStats.NativePicaShadow2dBackendShadowMapRenderTargetRequested) {
        if (scene.PicaShadow.Shadow2dShadowTextureWidth == 0 ||
            scene.PicaShadow.Shadow2dShadowTextureHeight == 0) {
            mStats.NativePicaShadow2dBackendShadowMapRenderTargetBlockedReason =
                "shadow2d_shadow_texture_dimension_register_decoded_zero_size";
        } else {
            if (mShadow2dFramebufferId < 0) {
                mShadow2dFramebufferId = mRenderingApi.CreateFramebuffer();
            }
            const bool nativeTargetReady =
                mRenderingApi.UpdateFramebufferParametersWithColorFormat(
                    mShadow2dFramebufferId,
                    scene.PicaShadow.Shadow2dShadowTextureWidth,
                    scene.PicaShadow.Shadow2dShadowTextureHeight,
                    1, false, true, true, true,
                    Fast::GfxFramebufferColorFormat::R32ui);
            mStats.NativePicaShadow2dBackendShadowMapRenderTargetNativeFormatSupported =
                nativeTargetReady;
            if (nativeTargetReady) {
                mShadow2dFramebufferWidth = scene.PicaShadow.Shadow2dShadowTextureWidth;
                mShadow2dFramebufferHeight = scene.PicaShadow.Shadow2dShadowTextureHeight;
                mStats.NativePicaShadow2dBackendShadowMapRenderTargetAllocated = true;
                mStats.NativePicaShadow2dBackendShadowMapFramebufferId = mShadow2dFramebufferId;
            } else {
                mStats.NativePicaShadow2dBackendShadowMapRenderTargetBlockedReason =
                    "shadow2d_backend_r32ui_render_target_format_not_supported";
            }
        }
    }
}

void Oot3dNativeFast3dRenderBackend::BeginSubmitQueue(Oot3dNativeSubmitQueue queue) {
    if (queue == Oot3dNativeSubmitQueue::Small && mConfig.ClearDepthBeforeSmallQueue) {
        mRenderingApi.ClearFramebuffer(false, true);
    }
}

Oot3dNativeTextureHandle Oot3dNativeFast3dRenderBackend::UploadTexture(
    std::string_view modelName, uint32_t textureIndex, const Oot3dNativeRenderTexture& texture) {
    TextureCacheKey cacheKey = BuildTextureCacheKey(modelName, textureIndex, texture);
    const auto cacheIt = mTextureCache.find(cacheKey);
    if (cacheIt != mTextureCache.end() && mTextures.find(cacheIt->second) != mTextures.end()) {
        mModelTextureHandles[{ std::string(modelName), textureIndex }] = cacheIt->second;
        ++mStats.TextureCacheHitCount;
        return cacheIt->second;
    }

    if (!IsTextureUploadable(texture)) {
        return kInvalidOot3dNativeTextureHandle;
    }

    const uint32_t rendererTextureId = mRenderingApi.NewTexture();
    mRenderingApi.SelectTexture(0, rendererTextureId);
    mRenderingApi.UploadTexture(texture.Rgba8.data(), texture.Width, texture.Height);
    uint32_t uploadedMipLevelCount = 1;
    size_t uploadedByteCount = texture.Rgba8.size();
    for (const auto& mip : texture.AdditionalMipLevels) {
        if (mip.Level != uploadedMipLevelCount || !mip.Rgba8Decoded || mip.Width == 0 || mip.Height == 0 ||
            mip.Rgba8.size() != static_cast<size_t>(mip.Width) * mip.Height * 4 ||
            !mRenderingApi.UploadTextureMipLevel(mip.Level, mip.Rgba8.data(), mip.Width, mip.Height)) {
            break;
        }
        ++uploadedMipLevelCount;
        uploadedByteCount += mip.Rgba8.size();
        ++mStats.TextureMipLevelUploadCount;
        mStats.TextureMipLevelUploadByteCount += mip.Rgba8.size();
    }
    mRenderingApi.SetSamplerParameters(0, mConfig.LinearFilter, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP);

    const auto handle = mNextHandle++;
    mTextures.emplace(handle, TextureBinding{ rendererTextureId, texture.Width, texture.Height,
                                              uploadedMipLevelCount, texture.HasNativeAlpha });
    mModelTextureHandles[{ std::string(modelName), textureIndex }] = handle;
    mTextureCache.emplace(std::move(cacheKey), handle);
    ++mStats.TextureUploadCount;
    mStats.TextureUploadByteCount += uploadedByteCount;
    mStats.TextureResidentCount = mTextures.size();
    return handle;
}

void Oot3dNativeFast3dRenderBackend::DrawBatch(std::string_view modelName, uint32_t batchIndex,
                                               const Oot3dNativeRenderBatch& batch,
                                               Oot3dNativeTextureHandle textureHandle,
                                               const Matrix4f& modelToWorld) {
    if (mCurrentModelOutsideFrustum) {
        ++mStats.FrustumCulledBatchCount;
        ++mStats.FrustumCulledModelBatchCounts[std::string(modelName)];
        return;
    }

    ++mStats.BatchDrawCount;
    if (batch.Vertices.empty()) {
        return;
    }

    const auto textureIt = mTextures.find(textureHandle);
    const bool textured = batch.Material.Textured && textureIt != mTextures.end();
    if (batch.Material.Textured && !textured) {
        ++mStats.MissingTextureBatchCount;
        ++mStats.MissingTextureBatchModelCounts[std::string(modelName)];
        return;
    }
    const bool texture1ColorAddRequested = textured && MaterialUsesTexture1ColorAddShader(batch.Material);
    const bool texture1ColorMultiplyRequested = textured && MaterialUsesTexture1ColorMultiplyShader(batch.Material);
    const bool texture0Texture1AddThenPrimaryColorModulateRequested =
        textured && MaterialUsesTexture0Texture1AddThenPrimaryColorModulateShader(batch.Material);
    const bool texture1Texture2MultiplyAddPreviousRequested =
        textured && MaterialUsesTexture1Texture2MultiplyAddPreviousShader(batch.Material);
    const bool texture0Texture1AddMultiplyTexture0Requested =
        textured && MaterialUsesTexture0Texture1AddMultiplyTexture0Shader(batch.Material);
    const bool texture1Requested = texture1ColorAddRequested || texture1ColorMultiplyRequested ||
                                   texture0Texture1AddThenPrimaryColorModulateRequested ||
                                   texture1Texture2MultiplyAddPreviousRequested ||
                                   texture0Texture1AddMultiplyTexture0Requested;
    Oot3dNativeTextureHandle secondaryTextureHandle = kInvalidOot3dNativeTextureHandle;
    auto secondaryTextureIt = mTextures.end();
    if (texture1Requested) {
        const auto modelTextureIt =
            mModelTextureHandles.find({ std::string(modelName),
                                        static_cast<uint32_t>(batch.Material.SecondaryTextureIndex) });
        if (modelTextureIt != mModelTextureHandles.end()) {
            secondaryTextureHandle = modelTextureIt->second;
            secondaryTextureIt = mTextures.find(secondaryTextureHandle);
        }
        if (secondaryTextureHandle == kInvalidOot3dNativeTextureHandle ||
            secondaryTextureIt == mTextures.end()) {
            ++mStats.MissingSecondaryTextureBindingCount;
        }
    }
    Oot3dNativeTextureHandle tertiaryTextureHandle = kInvalidOot3dNativeTextureHandle;
    auto tertiaryTextureIt = mTextures.end();
    if (texture1Texture2MultiplyAddPreviousRequested) {
        const auto modelTextureIt =
            mModelTextureHandles.find({ std::string(modelName),
                                        static_cast<uint32_t>(batch.Material.TertiaryTextureIndex) });
        if (modelTextureIt != mModelTextureHandles.end()) {
            tertiaryTextureHandle = modelTextureIt->second;
            tertiaryTextureIt = mTextures.find(tertiaryTextureHandle);
        }
        if (tertiaryTextureHandle == kInvalidOot3dNativeTextureHandle ||
            tertiaryTextureIt == mTextures.end()) {
            ++mStats.MissingTertiaryTextureBindingCount;
        }
    }
    const bool texture1ColorAddInput = texture1ColorAddRequested &&
                                       secondaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
                                       secondaryTextureIt != mTextures.end();
    const bool texture1ColorMultiplyInput = texture1ColorMultiplyRequested &&
                                            secondaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
                                            secondaryTextureIt != mTextures.end();
    const bool texture0Texture1AddMultiplyTexture0Input =
        texture0Texture1AddMultiplyTexture0Requested &&
        secondaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
        secondaryTextureIt != mTextures.end();
    const bool texture0Texture1AddThenPrimaryColorModulateInput =
        texture0Texture1AddThenPrimaryColorModulateRequested &&
        secondaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
        secondaryTextureIt != mTextures.end();
    const bool texture1Texture2MultiplyAddPreviousInput =
        texture1Texture2MultiplyAddPreviousRequested &&
        secondaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
        secondaryTextureIt != mTextures.end() &&
        tertiaryTextureHandle != kInvalidOot3dNativeTextureHandle &&
        tertiaryTextureIt != mTextures.end() &&
        mRenderingApi.SupportsOot3dPicaTexture2();
    if (texture1Texture2MultiplyAddPreviousRequested &&
        !mRenderingApi.SupportsOot3dPicaTexture2()) {
        ++mStats.NativePicaTexture2BackendUnsupportedBatchCount;
    }
    const bool texture1Input = texture1ColorAddInput || texture1ColorMultiplyInput ||
                               texture0Texture1AddThenPrimaryColorModulateInput ||
                               texture1Texture2MultiplyAddPreviousInput ||
                               texture0Texture1AddMultiplyTexture0Input;

    const bool alphaThreshold = batch.Material.AlphaTest;
    const auto nativePicaAlphaFunction = batch.Material.AlphaTest
                                             ? NativePicaCompareFunctionFromCmb(batch.Material.AlphaFunction)
                                             : std::nullopt;
    const bool nativePicaAlphaTest = nativePicaAlphaFunction.has_value();
    if (batch.Material.AlphaTest) {
        if (nativePicaAlphaTest) {
            ++mStats.NativePicaAlphaTestDecodedBatchCount;
        } else {
            ++mStats.NativePicaAlphaTestBackendUnsupportedBatchCount;
        }
    }
    const auto nativeBlendState = BuildNativeMaterialBlendState(batch.Material);
    const bool nativeAlphaBlend = nativeBlendState.has_value();
    const bool nativeBlendNeedsSourceAlpha =
        nativeBlendState.has_value() && NativeBlendStateNeedsSourceAlpha(*nativeBlendState);
    if (batch.Material.NativeBlendStateEnabled) {
        ++mStats.NativeBlendStateBatchCount;
        if (nativeAlphaBlend) {
            ++mStats.NativeBlendStateAppliedBatchCount;
        } else {
            ++mStats.NativeBlendStateUnsupportedBatchCount;
        }
    }
    if (batch.Material.NativePicaMaterialLutInputPacketAvailable) {
        ++mStats.NativePicaMaterialLutInputPacketAvailableBatchCount;
    }
    if (batch.Material.NativePicaMaterialLutInputPacketComplete) {
        ++mStats.NativePicaMaterialLutInputPacketCompleteBatchCount;
    }
    if (batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting) {
        ++mStats.NativePicaMaterialLutInputFragmentLightingBatchCount;
    }
    if (batch.Material.NativePicaMaterialLutInputEvaluationPending) {
        ++mStats.NativePicaMaterialLutInputEvaluationPendingBatchCount;
    }
    if (batch.Material.NativePicaMaterialLutInputEvaluationApplied) {
        ++mStats.NativePicaMaterialLutInputEvaluationAppliedBatchCount;
    }
    const bool runtimeVertexAlphaBlend = batch.Material.NativeRuntimeVertexAlphaBlend;
    const bool alphaBlend = mConfig.EnableAlphaBlend || nativeBlendNeedsSourceAlpha || runtimeVertexAlphaBlend;
    const bool alpha = alphaThreshold || alphaBlend ||
                       texture0Texture1AddMultiplyTexture0Requested;
    const bool textureColorAddendInput = textured && MaterialUsesTextureColorAddendShader(batch.Material);
    const bool textureColorAddTexture0AlphaInput =
        textureColorAddendInput &&
        batch.Material.TextureEnvProgram.TextureColorAddUsesTexture0Alpha;
    const bool textureColorMultiplierInput =
        textured && MaterialUsesTextureColorMultiplierShader(batch.Material) &&
        !texture0Texture1AddMultiplyTexture0Input;
    const bool texture1ColorAddTexture0AlphaInput =
        texture1ColorAddInput &&
        batch.Material.TextureEnvProgram.Texture1ColorAddUsesTexture0Alpha;
    const bool texture1ColorAddStageLocalPrimaryInput =
        texture1ColorAddInput && !texture1ColorAddTexture0AlphaInput &&
        batch.Material.TextureEnvProgram.PrimaryColorMultiplierResolved &&
        !Vec3IsIdentity(batch.Material.TextureEnvProgram.PrimaryColorMultiplier);
    const bool picaTextureEnvClamp = textured && batch.Material.TextureEnvProgram.ColorShaderPathApplied;
    const bool picaFogBatchDecoded =
        mPicaFog.Available && mPicaFog.FogEnabled &&
        batch.Material.NativePicaFogOverrideDecoded && batch.Material.NativePicaFogEnabled;
    if (picaFogBatchDecoded) {
        ++mStats.NativePicaFogDecodedBatchCount;
    }
    const bool picaFog = picaFogBatchDecoded && mPicaFog.UsedForRender;
    const bool picaSelfShadowCandidate =
        MaterialUsesNativePicaSelfShadowCandidate(mPicaShadow, batch.Material);
    const bool picaShadow2dTexCoord0WDecoded =
        batch.Material.NativePicaShadow2dTexCoord0WInputDecoded &&
        BatchHasNativePicaShadow2dTexCoord0W(batch);
    const bool picaShadow2dPrimaryRgbRoute =
        picaTextureEnvClamp &&
        picaSelfShadowCandidate &&
        mPicaShadow.Shadow2dVisualPassReady &&
        mStats.NativePicaShadow2dBackendShadowMapRenderTargetAllocated &&
        mStats.NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported &&
        batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded &&
        picaShadow2dTexCoord0WDecoded &&
        mPicaShadow.Shadow2dTextureShadowOrthographic;
    if (picaSelfShadowCandidate) {
        ++mStats.NativePicaSelfShadowCandidateBatchCount;
        mStats.NativePicaSelfShadowCandidateVertexCount += batch.Vertices.size();
        if (mStats.NativePicaShadow2dBackendPassRequestSupported) {
            mStats.NativePicaShadow2dBackendPassRequested = true;
            mStats.NativePicaShadow2dBackendPassPending =
                !mPicaShadow.SelfShadowShaderRouteDecoded ||
                !mStats.NativePicaShadow2dBackendShadowMapRenderTargetAllocated ||
                !mStats.NativePicaShadow2dBackendDepthEncodeShaderSupported;
        }
        if (mStats.NativePicaShadow2dVisualPassRequestSupported) {
            mStats.NativePicaShadow2dVisualPassRequested = true;
            mStats.NativePicaShadow2dVisualPassPending =
                !mPicaShadow.Shadow2dVisualPassReady;
        }
        if (batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded) {
            ++mStats.NativePicaShadow2dMaterialTextureProjectionDecodedBatchCount;
        }
        if (picaShadow2dTexCoord0WDecoded) {
            ++mStats.NativePicaShadow2dTexCoord0WInputDecodedBatchCount;
        }
        if (batch.Material.NativePicaSelfShadowApplied) {
            ++mStats.NativePicaSelfShadowAppliedBatchCount;
        } else {
            ++mStats.NativePicaSelfShadowPendingBatchCount;
        }
    }
    const bool vertexColorInput = !textured || batch.Material.VertexColorModulatesTexture ||
                                  textureColorAddendInput || textureColorMultiplierInput ||
                                  texture1Input;
    const bool textureAlphaMultipliesVertexAlpha =
        textured &&
        (batch.Material.TextureEnvProgram.Texture0PrimaryColorAlphaModulateResolved ||
         batch.Material.TextureEnvProgram.Texture0ConstantColorAlphaModulateResolved) &&
        !texture0Texture1AddMultiplyTexture0Input && alpha;
    const bool primaryAlphaUsesTextureEnvMultiplierOnly =
        batch.Material.TextureEnvProgram.Texture0ConstantColorAlphaModulateResolved;
    const bool vertexAlphaInput =
        (runtimeVertexAlphaBlend || textureAlphaMultipliesVertexAlpha ||
         texture0Texture1AddMultiplyTexture0Input) &&
        alpha;
    const bool vertexInput = vertexColorInput || vertexAlphaInput;
    const bool picaShadow2dPrimaryRgb = picaShadow2dPrimaryRgbRoute && vertexColorInput;
    const ColorRgba8 textureColorAddend = textureColorAddendInput
                                              ? batch.Material.TextureEnvProgram.TextureColorAddend
                                              : ColorRgba8{ 0, 0, 0, 255 };
    const Vec3f textureColorMultiplier = textureColorMultiplierInput
                                             ? batch.Material.TextureEnvProgram.TextureColorMultiplier
                                             : Vec3f{ 1.0f, 1.0f, 1.0f };
    const Vec3f primaryColorMultiplier =
        texture0Texture1AddMultiplyTexture0Input
            ? batch.Material.TextureEnvProgram.TextureColorMultiplier
            : picaTextureEnvClamp ? batch.Material.TextureEnvProgram.PrimaryColorMultiplier
                                  : Vec3f{ 1.0f, 1.0f, 1.0f };
    const float primaryAlphaMultiplier =
        textured && batch.Material.TextureEnvProgram.AlphaMultiplierResolved
            ? batch.Material.TextureEnvProgram.AlphaMultiplier
            : 1.0f;

    Fast::ShaderProgram* shader = SelectShader({ textured, alpha, alphaThreshold, nativePicaAlphaTest,
                                                 vertexColorInput,
                                                 vertexAlphaInput,
                                                 textureAlphaMultipliesVertexAlpha,
                                                 picaTextureEnvClamp, picaFog, picaShadow2dPrimaryRgb,
                                                 textureColorAddendInput,
                                                 textureColorAddTexture0AlphaInput,
                                                 textureColorMultiplierInput,
                                                 texture1ColorAddInput,
                                                 texture1ColorAddStageLocalPrimaryInput,
                                                 texture1ColorAddTexture0AlphaInput,
                                                 texture1ColorMultiplyInput,
                                                 texture0Texture1AddThenPrimaryColorModulateInput,
                                                 texture1Texture2MultiplyAddPreviousInput,
                                                 texture0Texture1AddMultiplyTexture0Input });
    Matrix4f modelToClip = MultiplyMatrices(mConfig.WorldToClip, modelToWorld);
    if (mConfig.AdjustForBackendClipParameters) {
        AdjustMatrixForBackendClipParameters(modelToClip, mRenderingApi.GetClipParameters());
    }
    const bool backendTransformsPosition =
        mRenderingApi.SetOot3dNativeTransform(&modelToClip.M[0][0]);
    if (alphaThreshold &&
        mRenderingApi.SetOot3dPicaAlphaTestShaderParameters(
            nativePicaAlphaTest, nativePicaAlphaFunction.value_or(1u),
            batch.Material.AlphaReference)) {
        if (nativePicaAlphaTest) {
            ++mStats.NativePicaAlphaTestAppliedBatchCount;
        }
    } else if (nativePicaAlphaTest) {
        ++mStats.NativePicaAlphaTestBackendUnsupportedBatchCount;
    }
    if (picaFog) {
        if (mRenderingApi.SetOot3dPicaFogShaderParameters(
                mPicaFog.LutWords.data(), mPicaFog.LutWordCount, mPicaFog.FogFlip,
                mPicaFogLutStateKey)) {
            ++mStats.NativePicaFogAppliedBatchCount;
            ++mStats.NativePicaFogFragmentParameterBindBatchCount;
        } else {
            ++mStats.NativePicaFogPendingBatchCount;
            ++mStats.NativePicaFogFragmentParameterBindFailureBatchCount;
        }
    } else if (picaFogBatchDecoded) {
        ++mStats.NativePicaFogPendingBatchCount;
    }
    uint8_t shaderInputCount = 0;
    bool shaderUsedTextures[2] = {};
    mRenderingApi.ShaderGetInfo(shader, &shaderInputCount, shaderUsedTextures);
    const bool secondaryTextureCoordInput = textured && shaderUsedTextures[1];
    const bool tertiaryTextureCoordInput = texture1Texture2MultiplyAddPreviousInput;
    const uint8_t expectedShaderInputCount =
        static_cast<uint8_t>((vertexInput ? 1 : 0) + (textureColorAddendInput ? 1 : 0) +
                             (textureColorMultiplierInput ? 1 : 0) +
                             (texture1ColorAddStageLocalPrimaryInput ? 1 : 0));
    if (shaderInputCount != expectedShaderInputCount) {
        ++mStats.ShaderInputLayoutMismatchCount;
    }
    mRenderingApi.SetDepthTestAndMask(batch.Material.DepthTest, batch.Material.DepthWrite);
    mRenderingApi.SetZmodeDecal(false);
    Fast::GfxNativeCullMode nativeCullMode = Fast::GfxNativeCullMode::KeepAll;
    if (batch.Material.PicaCullMode == Oot3dNativePicaCullMode::KeepClockwise) {
        nativeCullMode = Fast::GfxNativeCullMode::KeepClockwise;
    } else if (batch.Material.PicaCullMode == Oot3dNativePicaCullMode::KeepCounterClockwise) {
        nativeCullMode = Fast::GfxNativeCullMode::KeepCounterClockwise;
    }
    const bool nativeCullEnabled = batch.Material.PicaCullMode != Oot3dNativePicaCullMode::KeepAll;
    if (nativeCullEnabled) {
        ++mStats.NativeCullStateBatchCount;
    }
    if (mRenderingApi.SetNativeCullMode(nativeCullMode)) {
        if (nativeCullEnabled) {
            ++mStats.NativeCullStateAppliedBatchCount;
        }
    } else if (nativeCullEnabled) {
        ++mStats.NativeCullStateBackendUnsupportedBatchCount;
    }
    if (nativeBlendState.has_value()) {
        if (mRenderingApi.SetNativeBlendState(*nativeBlendState)) {
            ++mStats.NativeBlendStateBackendAppliedBatchCount;
        } else {
            ++mStats.NativeBlendStateBackendUnsupportedBatchCount;
            mRenderingApi.SetUseAlpha(alphaBlend || nativeBlendState->Enabled);
        }
    } else {
        mRenderingApi.SetUseAlpha(alphaBlend);
    }

    const auto applySampler = [&](int textureUnit, uint32_t mapperSlot, const TextureBinding& textureBinding) {
        Oot3dNativeRenderSamplerState resolvedSampler;
        if (mapperSlot < batch.Material.TextureMapperSamplerStates.size()) {
            const auto& sampler = batch.Material.TextureMapperSamplerStates[mapperSlot];
            if (sampler.Decoded) {
                resolvedSampler = sampler;
            }
        }
        if (!resolvedSampler.Decoded && textureUnit == 0 && batch.Material.NativeSamplerStateDecoded) {
            resolvedSampler.Decoded = true;
            resolvedSampler.MinFilter = batch.Material.NativeSamplerMinFilter;
            resolvedSampler.MagFilter = batch.Material.NativeSamplerMagFilter;
            resolvedSampler.WrapS = batch.Material.NativeSamplerWrapS;
            resolvedSampler.WrapT = batch.Material.NativeSamplerWrapT;
            resolvedSampler.LodBias = batch.Material.NativeSamplerLodBias;
        }
        if (!resolvedSampler.Decoded) {
            return;
        }

        ++mStats.NativeSamplerStateApplicationCount;
        const auto minFilter = NativeTextureFilter(resolvedSampler.MinFilter);
        const auto magFilter = NativeTextureFilter(resolvedSampler.MagFilter);
        const auto wrapS = NativeTextureWrap(resolvedSampler.WrapS);
        const auto wrapT = NativeTextureWrap(resolvedSampler.WrapT);
        const bool validMagFilter =
            magFilter == Fast::GfxNativeTextureFilter::Nearest ||
            magFilter == Fast::GfxNativeTextureFilter::Linear;
        if (minFilter.has_value() && magFilter.has_value() && validMagFilter &&
            wrapS.has_value() && wrapT.has_value()) {
            Fast::GfxNativeSamplerState nativeSampler;
            nativeSampler.MinFilter = *minFilter;
            nativeSampler.MagFilter = *magFilter;
            nativeSampler.WrapS = *wrapS;
            nativeSampler.WrapT = *wrapT;
            nativeSampler.LodBias = resolvedSampler.LodBias;
            nativeSampler.MaxMipLevel = textureBinding.MipLevelCount - 1;
            if (mRenderingApi.SetNativeSamplerParameters(textureUnit, nativeSampler)) {
                ++mStats.NativeSamplerStateBackendAppliedCount;
                return;
            }
        }
        ++mStats.NativeSamplerStateBackendUnsupportedCount;
        const bool linearFilter = resolvedSampler.MinFilter == 0x2601 ||
                                  resolvedSampler.MinFilter == 0x2701 ||
                                  resolvedSampler.MinFilter == 0x2703 ||
                                  resolvedSampler.MagFilter == 0x2601;
        mRenderingApi.SetSamplerParameters(textureUnit, linearFilter,
                                           NativeSamplerWrapMode(resolvedSampler.WrapS),
                                           NativeSamplerWrapMode(resolvedSampler.WrapT));
    };
    if (textured) {
        mRenderingApi.SelectTexture(0, textureIt->second.RendererTextureId);
        applySampler(0, batch.Material.TextureMapperSlot, textureIt->second);
        if (secondaryTextureCoordInput) {
            mRenderingApi.SelectTexture(1, texture1Input
                                               ? secondaryTextureIt->second.RendererTextureId
                                               : textureIt->second.RendererTextureId);
            if (texture1Input) {
                applySampler(1, batch.Material.SecondaryTextureMapperSlot, secondaryTextureIt->second);
            }
            ++mStats.SecondaryTextureBindingCount;
        }
        if (tertiaryTextureCoordInput) {
            mRenderingApi.SelectTexture(2, tertiaryTextureIt->second.RendererTextureId);
            applySampler(2, batch.Material.TertiaryTextureMapperSlot, tertiaryTextureIt->second);
            ++mStats.TertiaryTextureBindingCount;
        }
    }
    if (secondaryTextureCoordInput) {
        ++mStats.SecondaryTextureCoordBatchCount;
    }
    if (tertiaryTextureCoordInput) {
        ++mStats.TertiaryTextureCoordBatchCount;
    }
    if (picaShadow2dPrimaryRgb) {
        const bool shadowTextureBound =
            mRenderingApi.BindOot3dShadow2dTexture(mShadow2dFramebufferId, kOot3dShadow2dTextureUnit);
        const bool shadowParametersBound =
            mRenderingApi.SetOot3dShadow2dShaderParameters(
                mPicaShadow.Shadow2dTextureShadowCompareBias,
                mPicaShadow.Shadow2dTextureShadowOrthographic,
                mPicaShadow.Shadow2dLightingShadowInvert);
        if (!shadowTextureBound || !shadowParametersBound) {
            mStats.NativePicaShadow2dVisualPassPending = true;
        }
    }

    const size_t maxTrianglesPerDraw = std::max<size_t>(1, mConfig.MaxTrianglesPerDraw);
    const size_t triangleCount = batch.Vertices.size() / 3;
    const bool evaluateRuntimeLighting =
        mRuntimePicaLighting.has_value() && mCurrentModel != nullptr &&
        !mCurrentModel->NativeRuntimePicaLightingBaked;
    const bool packedVertexCacheEligible =
        backendTransformsPosition && mCurrentModel != nullptr &&
        mCurrentModel->NativeVertexDataCacheable && mCurrentModel->NativeGeometryId != 0;
    const uint64_t packedVertexStateKey = packedVertexCacheEligible
                                              ? BuildPackedVertexStateKey(
                                                    mCurrentModel->NativeGeometryContentVersion,
                                                    mRuntimePicaLightingStateKey,
                                                    evaluateRuntimeLighting,
                                                    mConfig.FlipTextureV,
                                                    maxTrianglesPerDraw,
                                                    textured,
                                                    secondaryTextureCoordInput,
                                                    tertiaryTextureCoordInput,
                                                    alpha,
                                                    vertexInput,
                                                    textureColorAddendInput,
                                                    textureColorMultiplierInput,
                                                    texture1ColorAddStageLocalPrimaryInput,
                                                    picaFog,
                                                    mPicaFog.Color,
                                                    picaShadow2dPrimaryRgb,
                                                    modelToWorld)
                                              : 0;
    Oot3dNativeRenderBatch lightingBatch;
    if (evaluateRuntimeLighting) {
        lightingBatch.Material = batch.Material;
        lightingBatch.Vertices.resize(1);
    }

    const auto submitChunk = [&](PackedVertexChunk& chunk, uint64_t contentVersion,
                                 bool usePersistentBuffer) {
        const bool submittedFromCache =
            usePersistentBuffer &&
            mRenderingApi.DrawTrianglesCached(chunk.CacheId, contentVersion,
                                              chunk.Vertices.data(), chunk.Vertices.size(),
                                              chunk.TriangleCount);
        if (!submittedFromCache) {
            mRenderingApi.DrawTriangles(chunk.Vertices.data(), chunk.Vertices.size(),
                                        chunk.TriangleCount);
        }
        ++mStats.BackendDrawCallCount;
        ++mStats.BackendDrawModelCounts[std::string(modelName)];
        mStats.TriangleCount += chunk.TriangleCount;
        mStats.VertexFloatCount += chunk.Vertices.size();
    };

    const auto packChunk = [&](std::vector<float>& vbo, size_t triangleOffset,
                               size_t chunkTriangleCount) {
        vbo.clear();
        const size_t requiredFloatCount =
            chunkTriangleCount * 3 *
            VertexStride(textured, secondaryTextureCoordInput, tertiaryTextureCoordInput,
                         alpha, vertexInput,
                         textureColorAddendInput, textureColorMultiplierInput,
                         texture1ColorAddStageLocalPrimaryInput,
                         picaFog, picaShadow2dPrimaryRgb);
        if (vbo.capacity() < requiredFloatCount) {
            vbo.reserve(requiredFloatCount);
        }

        const size_t vertexBegin = triangleOffset * 3;
        const size_t vertexEnd = vertexBegin + chunkTriangleCount * 3;
        for (size_t vertexIndex = vertexBegin; vertexIndex < vertexEnd; ++vertexIndex) {
            Oot3dNativeRenderVertex renderVertex = batch.Vertices[vertexIndex];
            if (evaluateRuntimeLighting) {
                lightingBatch.Vertices[0] = renderVertex;
                renderVertex.Color = EvaluateOot3dNativePicaLightingVertexColor(
                    mPicaLighting, *mCurrentModel, lightingBatch, modelToWorld, renderVertex);
            }
            AppendVertex(vbo, renderVertex, textured, secondaryTextureCoordInput,
                         tertiaryTextureCoordInput,
                         alpha, vertexInput,
                         textureColorAddendInput, textureColorAddend,
                         textureColorMultiplierInput, textureColorMultiplier,
                         primaryColorMultiplier, primaryAlphaMultiplier,
                         texture1ColorAddStageLocalPrimaryInput,
                         texture0Texture1AddMultiplyTexture0Input,
                          primaryAlphaUsesTextureEnvMultiplierOnly,
                          picaFog, picaShadow2dPrimaryRgb,
                          modelToWorld, backendTransformsPosition);
        }
    };

    if (packedVertexCacheEligible) {
        const auto cacheKey = std::make_pair(mCurrentModel->NativeGeometryId, batchIndex);
        auto cacheIt = mPackedBatchCache.find(cacheKey);
        if (cacheIt != mPackedBatchCache.end() &&
            cacheIt->second.StateKey == packedVertexStateKey) {
            ++mStats.PackedVertexCacheHitCount;
            cacheIt->second.LastUseSequence = ++mPackedCacheUseSequence;
            for (auto& chunk : cacheIt->second.Chunks) {
                submitChunk(chunk, cacheIt->second.ContentVersion, true);
            }
            return;
        }

        ++mStats.PackedVertexCacheMissCount;
        if (cacheIt == mPackedBatchCache.end() &&
            mPackedBatchCache.size() >= kPackedBatchCacheEntryLimit) {
            const auto oldest = std::min_element(
                mPackedBatchCache.begin(), mPackedBatchCache.end(),
                [](const auto& left, const auto& right) {
                    return left.second.LastUseSequence < right.second.LastUseSequence;
                });
            if (oldest != mPackedBatchCache.end()) {
                mPackedBatchCache.erase(oldest);
            }
        }

        auto& cached = mPackedBatchCache[cacheKey];
        cached.StateKey = packedVertexStateKey;
        cached.ContentVersion = mNextPackedContentVersion++;
        if (cached.ContentVersion == 0) {
            cached.ContentVersion = mNextPackedContentVersion++;
        }
        cached.LastUseSequence = ++mPackedCacheUseSequence;
        cached.Chunks.clear();

        size_t triangleOffset = 0;
        uint32_t chunkIndex = 0;
        while (triangleOffset < triangleCount) {
            const size_t chunkTriangleCount =
                std::min(maxTrianglesPerDraw, triangleCount - triangleOffset);
            auto& chunk = cached.Chunks.emplace_back();
            chunk.CacheId = BuildPackedVertexCacheId(mCurrentModel->NativeGeometryId,
                                                     batchIndex, chunkIndex++);
            chunk.TriangleCount = chunkTriangleCount;
            packChunk(chunk.Vertices, triangleOffset, chunkTriangleCount);
            triangleOffset += chunkTriangleCount;
        }
        for (auto& chunk : cached.Chunks) {
            submitChunk(chunk, cached.ContentVersion, true);
        }
        return;
    }

    size_t triangleOffset = 0;
    while (triangleOffset < triangleCount) {
        const size_t chunkTriangleCount =
            std::min(maxTrianglesPerDraw, triangleCount - triangleOffset);
        PackedVertexChunk chunk;
        chunk.TriangleCount = chunkTriangleCount;
        mVertexScratch.clear();
        packChunk(mVertexScratch, triangleOffset, chunkTriangleCount);
        chunk.Vertices.swap(mVertexScratch);
        submitChunk(chunk, 0, false);
        mVertexScratch.swap(chunk.Vertices);
        triangleOffset += chunkTriangleCount;
    }
}

void Oot3dNativeFast3dRenderBackend::DrawKankyoPrimitive(
    const Oot3dNativeKankyoPrimitiveBackendInputState& input,
    Oot3dNativeTextureHandle textureHandle) {
    ++mStats.NativeKankyoPrimitiveBackendInputSubmitCount;
    mStats.NativeKankyoPrimitiveBackendInputResolved =
        mStats.NativeKankyoPrimitiveBackendInputResolved || input.ReadyForBackendInput;
    mStats.NativeKankyoPrimitiveTextureName = input.TextureName;
    mStats.NativeKankyoPrimitiveTerminalTextureName = input.TerminalTextureName;
    mStats.NativeKankyoPrimitiveVisibilityTarget = input.NativeLensVisibilityTarget;
    mStats.NativeKankyoPrimitiveVisibilityScreenGateResolved =
        input.NativeLensVisibilityScreenGateResolved;
    mStats.NativeKankyoPrimitiveVisibilitySceneOcclusionResolved =
        input.NativeLensVisibilitySceneOcclusionResolved;
    mStats.NativeKankyoPrimitiveVisibilitySourceInViewport =
        input.NativeLensVisibilitySourceInViewport;
    mStats.NativeKankyoPrimitiveVisibilitySourceOccluded =
        input.NativeLensVisibilitySourceOccluded;
    mStats.NativeKankyoPrimitiveColorPassPrimitiveValue = input.ColorPassPrimitiveValue;
    mStats.NativeKankyoPrimitiveAlphaPassPrimitiveValue = input.AlphaPassPrimitiveValue;
    mStats.NativeKankyoPrimitiveTextureHasNativeAlpha =
        mStats.NativeKankyoPrimitiveTextureHasNativeAlpha || input.TextureHasNativeAlpha;
    mStats.NativeKankyoPrimitivePicaAlphaBlendSemanticsRequired =
        mStats.NativeKankyoPrimitivePicaAlphaBlendSemanticsRequired ||
        input.NativePicaAlphaBlendSemanticsRequired;
    mStats.NativeKankyoPrimitivePicaAlphaBlendSemanticsResolved =
        mStats.NativeKankyoPrimitivePicaAlphaBlendSemanticsResolved ||
        input.NativePicaAlphaBlendSemanticsResolved;
    mStats.NativeKankyoPrimitiveOverlayVertexInputCount += input.OverlayVertices.size();
    mStats.NativeKankyoPrimitiveExpectedExpandedVertexCount += input.QuadBatchExpandedVertexCount;
    mStats.NativeKankyoPrimitiveRuntime28CQuadLaneVertexInputCount +=
        input.Runtime28CQuadLaneVertexInputCount;
    mStats.NativeKankyoPrimitiveRuntime28CDrawCount += input.Runtime28CDrawCount;
    mStats.NativeKankyoPrimitiveRuntime28CQuadLaneMaterialized =
        mStats.NativeKankyoPrimitiveRuntime28CQuadLaneMaterialized ||
        input.Runtime28CQuadLaneMaterialized;
    mStats.NativeKankyoPrimitiveRuntime28CPositionProducerResolved =
        mStats.NativeKankyoPrimitiveRuntime28CPositionProducerResolved ||
        input.Runtime28CPositionProducerResolved;
    mStats.NativeKankyoPrimitiveRuntime28CPositionRuntimeInputResolved =
        mStats.NativeKankyoPrimitiveRuntime28CPositionRuntimeInputResolved ||
        input.Runtime28CPositionRuntimeInputResolved;
    if (textureHandle != kInvalidOot3dNativeTextureHandle &&
        mTextures.find(textureHandle) != mTextures.end()) {
        ++mStats.NativeKankyoPrimitiveDecodedTextureSubmitCount;
    } else {
        ++mStats.NativeKankyoPrimitiveMissingTextureSubmitCount;
    }

    const auto textureIt = mTextures.find(textureHandle);
    if (!input.ReadyForBackendInput) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            !input.SourceStatus.empty()
                ? input.SourceStatus
                : "native_kankyo_primitive_backend_input_not_ready";
        return;
    }
    if (!mConfig.EnableKankyoPrimitiveRender) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            "native_kankyo_primitive_render_disabled_by_config";
        return;
    }
    if (!input.Runtime28CPositionRuntimeInputResolved ||
        input.Runtime28CPositionRecords.empty()) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            !input.Runtime28CPositionRuntimeInputBlockedReason.empty()
                ? input.Runtime28CPositionRuntimeInputBlockedReason
                : "native_lens_runtime_position_records_not_materialized";
        return;
    }
    if (textureIt == mTextures.end()) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            "native_kankyo_primitive_decoded_texture_not_uploaded";
        return;
    }
    if (input.Runtime28CVisibleBatches.empty()) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            "native_kankyo_primitive_visible_batch_inputs_missing";
        return;
    }
    if (!input.ReadyForBackendRender) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            !input.VisibleBackendBlockedReason.empty()
                ? input.VisibleBackendBlockedReason
                : "native_kankyo_primitive_visible_backend_submit_not_resolved";
        return;
    }

    if (!input.NativeLensVisibilityScreenGateResolved) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            !input.NativeLensVisibilitySourceStatus.empty()
                ? input.NativeLensVisibilitySourceStatus
                : "native_lens_visibility_screen_gate_not_resolved";
        return;
    }
    if (input.Runtime28CPositionProjectionScaleX == 0.0f ||
        input.Runtime28CPositionProjectionScaleY == 0.0f ||
        !input.Runtime28CPositionRuntimeScaleResolved) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            "native_lens_projection_or_runtime_scale_not_resolved";
        return;
    }
    mKankyoLensVisibilityFactor = NativeSmoothStepToF(
        mKankyoLensVisibilityFactor,
        std::clamp(input.NativeLensVisibilityTarget, 0.0f,
                   input.NativeLensVisibilityVisibleTarget),
        input.NativeLensVisibilityScale,
        input.NativeLensVisibilityMaxStep,
        input.NativeLensVisibilityMinStep);
    mStats.NativeKankyoPrimitiveVisibilityFactor = mKankyoLensVisibilityFactor;
    if (!(mKankyoLensVisibilityFactor > 0.0f)) {
        ++mStats.NativeKankyoPrimitiveVisibilitySuppressedDrawCount;
        mStats.NativeKankyoPrimitiveVisibleDrawPending = false;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason.clear();
        return;
    }

    const auto findBatch =
        [&input](const Oot3dNativeKankyoRuntime28CPositionRecord& record)
            -> const Oot3dNativeKankyoRuntime28CVisibleBatchInput* {
        for (const auto& batch : input.Runtime28CVisibleBatches) {
            if (batch.SourceElementIndex == record.SourceElementIndex) {
                return &batch;
            }
        }
        for (const auto& batch : input.Runtime28CVisibleBatches) {
            if (batch.VisibleBatchIndex == record.VisibleBatchIndex) {
                return &batch;
            }
        }
        return nullptr;
    };

    const bool picaFogBatchDecoded = false;
    if (picaFogBatchDecoded) {
        ++mStats.NativePicaFogDecodedBatchCount;
        mStats.NativeKankyoPrimitivePicaFogDecoded = true;
    }
    const bool picaFog =
        picaFogBatchDecoded && mPicaFog.ShaderSupported && mPicaFog.LutShaderSupported &&
        mPicaFog.LutWordCount > 0;
    if (picaFog) {
        ++mStats.NativePicaFogAppliedBatchCount;
        mStats.NativeKankyoPrimitivePicaFogApplied = true;
    } else if (picaFogBatchDecoded) {
        ++mStats.NativePicaFogPendingBatchCount;
        mStats.NativeKankyoPrimitivePicaFogPending = true;
    }

    const auto clipParameters = mRenderingApi.GetClipParameters();
    const auto appendScreenVertex =
        [this, &input, &clipParameters, picaFog](std::vector<float>& vbo,
                                                 float screenX, float screenY, float depth,
                                                 float u, float v, ColorRgba8 color) {
        const float projectionScaleX = input.Runtime28CPositionProjectionScaleX;
        const float projectionScaleY = input.Runtime28CPositionProjectionScaleY;
        const float screenCenterX = input.Runtime28CPositionScreenCenterX;
        const float projectionBaseY = input.Runtime28CPositionProjectionBaseY;
        ClipVertex clip{
            (screenX - screenCenterX) / projectionScaleX,
            (screenY - projectionBaseY) / projectionScaleY,
            std::isfinite(depth) ? depth : 0.0f,
            1.0f,
        };
        if (mConfig.AdjustForBackendClipParameters) {
            if (clipParameters.z_is_from_0_to_1) {
                clip.Z = (clip.Z + clip.W) / 2.0f;
            }
            if (clipParameters.invertY) {
                clip.Y = -clip.Y;
            }
        }

        vbo.push_back(clip.X);
        vbo.push_back(clip.Y);
        vbo.push_back(clip.Z);
        vbo.push_back(clip.W);
        vbo.push_back(u);
        vbo.push_back(mConfig.FlipTextureV ? 1.0f - v : v);
        if (picaFog) {
            vbo.push_back(ColorChannel(mPicaFog.Color.R));
            vbo.push_back(ColorChannel(mPicaFog.Color.G));
            vbo.push_back(ColorChannel(mPicaFog.Color.B));
            vbo.push_back(0.0f);
        }
        vbo.push_back(ColorChannel(color.R));
        vbo.push_back(ColorChannel(color.G));
        vbo.push_back(ColorChannel(color.B));
        vbo.push_back(ColorAlpha(color.A));
    };

    Fast::ShaderProgram* shader =
        SelectShader({ true, true, false, false, true, true,
                       false,
                       false, picaFog, false,
                       false, false, false, false, false, false });
    const Matrix4f clipSpaceIdentity = IdentityMatrix();
    mRenderingApi.SetOot3dNativeTransform(&clipSpaceIdentity.M[0][0]);
    uint8_t shaderInputCount = 0;
    bool shaderUsedTextures[2] = {};
    mRenderingApi.ShaderGetInfo(shader, &shaderInputCount, shaderUsedTextures);
    if (shaderInputCount != 1) {
        ++mStats.ShaderInputLayoutMismatchCount;
    }
    mRenderingApi.SetDepthTestAndMask(false, false);
    mRenderingApi.SetZmodeDecal(false);
    mRenderingApi.SetNativeCullMode(Fast::GfxNativeCullMode::KeepAll);
    const auto kankyoBlendState = SourceAlphaAdditiveBlendState();
    if (mRenderingApi.SetNativeBlendState(kankyoBlendState)) {
        ++mStats.NativeBlendStateBackendAppliedBatchCount;
    } else {
        ++mStats.NativeBlendStateBackendUnsupportedBatchCount;
        mRenderingApi.SetUseAlpha(true);
    }
    const size_t maxTrianglesPerDraw = std::max<size_t>(1, mConfig.MaxTrianglesPerDraw);
    auto& vbo = mVertexScratch;
    vbo.clear();
    const size_t requiredFloatCount =
        maxTrianglesPerDraw * 3 *
        VertexStride(true, false, false, true, true, false, false, false, picaFog, false);
    if (vbo.capacity() < requiredFloatCount) {
        vbo.reserve(requiredFloatCount);
    }

    size_t chunkTriangleCount = 0;
    size_t emittedVertexCount = 0;
    uint32_t selectedRendererTextureId = std::numeric_limits<uint32_t>::max();
    const auto flushChunk = [&]() {
        if (chunkTriangleCount == 0) {
            return;
        }
        mRenderingApi.DrawTriangles(vbo.data(), vbo.size(), chunkTriangleCount);
        ++mStats.BackendDrawCallCount;
        ++mStats.NativeKankyoPrimitiveVisibleDrawCallCount;
        mStats.TriangleCount += chunkTriangleCount;
        mStats.NativeKankyoPrimitiveVisibleTriangleCount += chunkTriangleCount;
        mStats.VertexFloatCount += vbo.size();
        vbo.clear();
        chunkTriangleCount = 0;
    };

    for (const auto& record : input.Runtime28CPositionRecords) {
        const bool primaryBatchElement =
            record.SourceElementIndex <= input.PrimaryBatchLastElementIndex;
        if (!primaryBatchElement) {
            continue;
        }
        const auto* batch = findBatch(record);
        if (batch == nullptr) {
            continue;
        }
        const auto& textureBinding = textureIt->second;
        if (selectedRendererTextureId != textureBinding.RendererTextureId) {
            flushChunk();
            selectedRendererTextureId = textureBinding.RendererTextureId;
            mRenderingApi.SelectTexture(0, selectedRendererTextureId);
        }
        const float nativeScale = record.RuntimeScale;
        if (nativeScale <= 0.0f) {
            continue;
        }
        const float aspect =
            textureBinding.Height == 0
                ? 1.0f
                : static_cast<float>(textureBinding.Width) /
                      static_cast<float>(textureBinding.Height);
        const float halfHeight = nativeScale * 0.5f;
        const float halfWidth = halfHeight * aspect;
        const float centerX = static_cast<float>(record.RuntimePosition.X);
        const float centerY = static_cast<float>(record.RuntimePosition.Y);
        const float depth = static_cast<float>(record.RuntimePosition.Z);
        ColorRgba8 color = batch->LensHalationColor;
        color.A = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(
                static_cast<double>(color.A) * mKankyoLensVisibilityFactor)),
            0, 255));

        if (chunkTriangleCount + 2 > maxTrianglesPerDraw) {
            flushChunk();
        }
        appendScreenVertex(vbo, centerX - halfWidth, centerY - halfHeight, depth, 0.0f, 0.0f, color);
        appendScreenVertex(vbo, centerX + halfWidth, centerY - halfHeight, depth, 1.0f, 0.0f, color);
        appendScreenVertex(vbo, centerX - halfWidth, centerY + halfHeight, depth, 0.0f, 1.0f, color);
        appendScreenVertex(vbo, centerX - halfWidth, centerY + halfHeight, depth, 0.0f, 1.0f, color);
        appendScreenVertex(vbo, centerX + halfWidth, centerY - halfHeight, depth, 1.0f, 0.0f, color);
        appendScreenVertex(vbo, centerX + halfWidth, centerY + halfHeight, depth, 1.0f, 1.0f, color);
        chunkTriangleCount += 2;
        emittedVertexCount += 6;
    }
    flushChunk();

    if (emittedVertexCount == 0) {
        mStats.NativeKankyoPrimitiveVisibleDrawPending = true;
        mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason =
            "native_kankyo_primitive_runtime_records_produced_no_visible_backend_vertices";
        return;
    }

    mStats.NativeKankyoPrimitiveVisibleVertexCount += emittedVertexCount;
    mStats.NativeKankyoPrimitiveVisibleDrawPending = false;
    mStats.NativeKankyoPrimitiveVisibleDrawBlockedReason.clear();
}

void Oot3dNativeFast3dRenderBackend::ReleaseTextures() {
    for (const auto& textureEntry : mTextures) {
        mRenderingApi.DeleteTexture(textureEntry.second.RendererTextureId);
    }
    mTextures.clear();
    mModelTextureHandles.clear();
    mTextureCache.clear();
    mStats.TextureResidentCount = 0;
}

void Oot3dNativeFast3dRenderBackend::SetConfig(Oot3dNativeFast3dRenderConfig config) {
    if (ConfigRequiresTextureRebuild(mConfig, config)) {
        ReleaseTextures();
    }
    mConfig = std::move(config);
}

void Oot3dNativeFast3dRenderBackend::BeginModel(const Oot3dNativeRenderModel& model) {
    mCurrentModel = &model;
    const auto clipParameters = mRenderingApi.GetClipParameters();
    mCurrentModelOutsideFrustum =
        ModelBoundsOutsideClipVolume(model, mConfig.WorldToClip, clipParameters,
                                     mConfig.AdjustForBackendClipParameters);
}

void Oot3dNativeFast3dRenderBackend::SetRuntimePicaLighting(
    std::optional<Oot3dNativePicaLightingRenderState> runtimeLighting) {
    mRuntimePicaLightingStateKey = BuildRuntimePicaLightingStateKey(runtimeLighting);
    mRuntimePicaLighting = std::move(runtimeLighting);
}

void Oot3dNativeFast3dRenderBackend::SetRuntimePicaFog(
    std::optional<Oot3dNativePicaFogState> runtimeFog) {
    mRuntimePicaFog = std::move(runtimeFog);
}

void Oot3dNativeFast3dRenderBackend::ReleaseCurrentShaderBinding() {
    if (mCurrentShader != nullptr) {
        mRenderingApi.UnloadShader(mCurrentShader);
        mCurrentShader = nullptr;
    }
}

const Oot3dNativeFast3dRenderConfig& Oot3dNativeFast3dRenderBackend::Config() const {
    return mConfig;
}

const Oot3dNativeFast3dRenderStats& Oot3dNativeFast3dRenderBackend::Stats() const {
    return mStats;
}

Oot3dNativeFast3dRenderBackend::TextureCacheKey Oot3dNativeFast3dRenderBackend::BuildTextureCacheKey(
    std::string_view modelName, uint32_t textureIndex, const Oot3dNativeRenderTexture& texture) {
    TextureCacheKey key;
    key.ModelName = std::string(modelName);
    key.TextureName = texture.Name;
    key.TextureIndex = textureIndex;
    key.SourceIndex = texture.SourceIndex;
    key.Width = texture.Width;
    key.Height = texture.Height;
    key.TextureFormat = texture.TextureFormat;
    key.MipmapCount = std::max<uint32_t>(1, texture.MipmapCount);
    key.HasNativeAlpha = texture.HasNativeAlpha;
    key.Rgba8ByteCount = texture.Rgba8ByteCount != 0 ? texture.Rgba8ByteCount : texture.Rgba8.size();
    key.Rgba8Hash = texture.Rgba8HashAvailable ? texture.Rgba8Hash : HashTextureBytes(texture.Rgba8);
    return key;
}

bool Oot3dNativeFast3dRenderBackend::ConfigRequiresTextureRebuild(
    const Oot3dNativeFast3dRenderConfig& before, const Oot3dNativeFast3dRenderConfig& after) {
    return before.LinearFilter != after.LinearFilter;
}

Fast::ShaderProgram* Oot3dNativeFast3dRenderBackend::SelectShader(const ShaderKey& key) {
    auto shaderIt = mShaders.find(key);
    if (shaderIt == mShaders.end()) {
        const uint32_t colorInput = key.Textured ? SHADER_TEXEL0 : SHADER_INPUT_1;
        const uint32_t alphaInput =
            key.VertexAlphaInput ? SHADER_INPUT_1 : (key.Textured ? SHADER_TEXEL0 : SHADER_INPUT_1);
        uint64_t shaderId0 = BuildDirectColorShaderId0(
            colorInput, alphaInput, key.TextureAlphaMultipliesVertexAlpha);
        if (key.Textured && key.Texture0Texture1AddMultiplyTexture0Input) {
            shaderId0 = BuildTexture0Texture1AddMultiplyTexture0ShaderId0();
        } else if (key.Textured && key.Texture0Texture1AddThenPrimaryColorModulateInput) {
            shaderId0 = BuildTexture0Texture1AddThenVertexColorMultiplyShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha);
        } else if (key.Textured && key.Texture1ColorAddTexture0AlphaInput) {
            shaderId0 = BuildTexture0AlphaTexture1VertexColorAddShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha);
        } else if (key.Textured && key.Texture1ColorMultiplyInput) {
            shaderId0 = BuildTexture0Texture1VertexColorMultiplyShaderId0();
        } else if (key.Textured && key.Texture1ColorAddInput) {
            shaderId0 = BuildTexture0Texture1VertexColorAddShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha,
                key.Texture1ColorAddStageLocalPrimaryInput);
        } else if (key.Textured && key.TextureColorAddendInput && key.TextureColorMultiplierInput) {
            shaderId0 = BuildTextureVertexColorAddMultiplyShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha,
                key.TextureColorAddTexture0AlphaInput);
        } else if (key.Textured && key.TextureColorMultiplierInput) {
            shaderId0 = BuildTextureVertexColorMultiplyShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha);
        } else if (key.Textured && key.TextureColorAddendInput) {
            shaderId0 = BuildTextureVertexColorAddShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha,
                key.TextureColorAddTexture0AlphaInput);
        } else if (key.Textured && key.VertexColorInput) {
            shaderId0 = BuildTextureVertexColorShaderId0(
                alphaInput, key.TextureAlphaMultipliesVertexAlpha);
        }
        const bool twoCycle = key.TextureColorMultiplierInput ||
                              key.Texture1ColorAddInput ||
                              key.Texture1ColorAddTexture0AlphaInput ||
                              key.Texture1ColorMultiplyInput ||
                              key.Texture0Texture1AddThenPrimaryColorModulateInput ||
                              key.Texture0Texture1AddMultiplyTexture0Input;
        const uint64_t shaderId1 = BuildShaderId1(key.Alpha, key.AlphaThreshold,
                                                   key.PicaTextureEnvClamp,
                                                   key.PicaFog,
                                                   key.PicaAlphaTest,
                                                   key.PicaShadow2dPrimaryRgb,
                                                   key.Texture1Texture2MultiplyAddPreviousInput,
                                                   key.Texture0Texture1AddMultiplyTexture0Input,
                                                   twoCycle);
        Fast::ShaderProgram* shader = mRenderingApi.LookupShader(shaderId0, shaderId1);
        if (shader == nullptr) {
            shader = mRenderingApi.CreateAndLoadNewShader(shaderId0, shaderId1);
        }
        shaderIt = mShaders.emplace(key, shader).first;
    }

    Fast::ShaderProgram* shader = shaderIt->second;
    if (shader != mCurrentShader) {
        if (mCurrentShader != nullptr) {
            mRenderingApi.UnloadShader(mCurrentShader);
        }
        mCurrentShader = shader;
        ++mStats.ShaderSwitchCount;
    }
    mRenderingApi.LoadShader(shader);
    return shader;
}

void Oot3dNativeFast3dRenderBackend::AppendVertex(std::vector<float>& vbo, const Oot3dNativeRenderVertex& vertex,
                                                  bool textured, bool secondaryTextureCoordInput,
                                                  bool tertiaryTextureCoordInput,
                                                  bool alpha, bool vertexColorInput,
                                                  bool textureColorAddendInput,
                                                  ColorRgba8 textureColorAddend,
                                                  bool textureColorMultiplierInput,
                                                  Vec3f textureColorMultiplier,
                                                  Vec3f primaryColorMultiplier,
                                                  float primaryAlphaMultiplier,
                                                  bool texture1ColorAddStageLocalPrimaryInput,
                                                  bool primaryColorUsesTextureEnvMultiplierOnly,
                                                  bool primaryAlphaUsesTextureEnvMultiplierOnly,
                                                  bool picaFog,
                                                  bool picaShadow2dPrimaryRgb,
                                                  const Matrix4f& modelToWorld,
                                                  bool backendTransformsPosition) {
    if (backendTransformsPosition) {
        vbo.push_back(vertex.Position.X);
        vbo.push_back(vertex.Position.Y);
        vbo.push_back(vertex.Position.Z);
        vbo.push_back(1.0f);
    } else {
        const ClipVertex world = TransformPosition(modelToWorld, vertex.Position);
        ClipVertex clip = TransformPosition(mConfig.WorldToClip, world);
        const auto clipParameters = mRenderingApi.GetClipParameters();
        if (mConfig.AdjustForBackendClipParameters) {
            if (clipParameters.z_is_from_0_to_1) {
                clip.Z = (clip.Z + clip.W) / 2.0f;
            }
            if (clipParameters.invertY) {
                clip.Y = -clip.Y;
            }
        }

        vbo.push_back(clip.X);
        vbo.push_back(clip.Y);
        vbo.push_back(clip.Z);
        vbo.push_back(clip.W);
    }

    if (textured) {
        vbo.push_back(vertex.Uv0.X);
        vbo.push_back(mConfig.FlipTextureV ? 1.0f - vertex.Uv0.Y : vertex.Uv0.Y);
    }
    if (secondaryTextureCoordInput) {
        vbo.push_back(vertex.Uv1.X);
        vbo.push_back(mConfig.FlipTextureV ? 1.0f - vertex.Uv1.Y : vertex.Uv1.Y);
    }
    if (tertiaryTextureCoordInput) {
        vbo.push_back(vertex.Uv2.X);
        vbo.push_back(mConfig.FlipTextureV ? 1.0f - vertex.Uv2.Y : vertex.Uv2.Y);
    }

    if (picaFog) {
        vbo.push_back(ColorChannel(mPicaFog.Color.R));
        vbo.push_back(ColorChannel(mPicaFog.Color.G));
        vbo.push_back(ColorChannel(mPicaFog.Color.B));
        vbo.push_back(0.0f);
    }

    if (vertexColorInput) {
        vbo.push_back((primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.R)) *
                      primaryColorMultiplier.X);
        vbo.push_back((primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.G)) *
                      primaryColorMultiplier.Y);
        vbo.push_back((primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.B)) *
                      primaryColorMultiplier.Z);
        if (alpha) {
            vbo.push_back((primaryAlphaUsesTextureEnvMultiplierOnly ? 1.0f : ColorAlpha(vertex.Color.A)) *
                          primaryAlphaMultiplier);
        }
    }

    if (textureColorAddendInput) {
        vbo.push_back(ColorChannel(textureColorAddend.R));
        vbo.push_back(ColorChannel(textureColorAddend.G));
        vbo.push_back(ColorChannel(textureColorAddend.B));
        if (alpha) {
            vbo.push_back(ColorAlpha(textureColorAddend.A));
        }
    }

    if (textureColorMultiplierInput) {
        vbo.push_back(textureColorMultiplier.X);
        vbo.push_back(textureColorMultiplier.Y);
        vbo.push_back(textureColorMultiplier.Z);
        if (alpha) {
            vbo.push_back(1.0f);
        }
    }

    if (texture1ColorAddStageLocalPrimaryInput) {
        vbo.push_back(primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.R));
        vbo.push_back(primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.G));
        vbo.push_back(primaryColorUsesTextureEnvMultiplierOnly ? 1.0f : ColorChannel(vertex.Color.B));
        if (alpha) {
            vbo.push_back((primaryAlphaUsesTextureEnvMultiplierOnly ? 1.0f : ColorAlpha(vertex.Color.A)) *
                          primaryAlphaMultiplier);
        }
    }

    if (picaShadow2dPrimaryRgb) {
        vbo.push_back(vertex.NativePicaShadow2dTexCoord0.X);
        vbo.push_back(mConfig.FlipTextureV ? 1.0f - vertex.NativePicaShadow2dTexCoord0.Y
                                           : vertex.NativePicaShadow2dTexCoord0.Y);
        vbo.push_back(vertex.NativePicaShadow2dTexCoord0W);
    }
}

nlohmann::json Oot3dNativeFast3dRenderStatsToJson(const Oot3dNativeFast3dRenderStats& stats) {
    return {
        { "texture_upload_count", stats.TextureUploadCount },
        { "texture_upload_byte_count", stats.TextureUploadByteCount },
        { "texture_mip_level_upload_count", stats.TextureMipLevelUploadCount },
        { "texture_mip_level_upload_byte_count", stats.TextureMipLevelUploadByteCount },
        { "texture_cache_hit_count", stats.TextureCacheHitCount },
        { "texture_resident_count", stats.TextureResidentCount },
        { "batch_draw_count", stats.BatchDrawCount },
        { "backend_draw_call_count", stats.BackendDrawCallCount },
        { "triangle_count", stats.TriangleCount },
        { "vertex_float_count", stats.VertexFloatCount },
        { "packed_vertex_cache_hit_count", stats.PackedVertexCacheHitCount },
        { "packed_vertex_cache_miss_count", stats.PackedVertexCacheMissCount },
        { "frustum_culled_batch_count", stats.FrustumCulledBatchCount },
        { "frustum_culled_model_batch_counts", stats.FrustumCulledModelBatchCounts },
        { "shader_switch_count", stats.ShaderSwitchCount },
        { "missing_texture_batch_count", stats.MissingTextureBatchCount },
        { "backend_draw_model_counts", stats.BackendDrawModelCounts },
        { "missing_texture_batch_model_counts", stats.MissingTextureBatchModelCounts },
        { "secondary_texture_coord_batch_count", stats.SecondaryTextureCoordBatchCount },
        { "secondary_texture_binding_count", stats.SecondaryTextureBindingCount },
        { "missing_secondary_texture_binding_count", stats.MissingSecondaryTextureBindingCount },
        { "tertiary_texture_coord_batch_count", stats.TertiaryTextureCoordBatchCount },
        { "tertiary_texture_binding_count", stats.TertiaryTextureBindingCount },
        { "missing_tertiary_texture_binding_count", stats.MissingTertiaryTextureBindingCount },
        { "native_pica_texture2_backend_unsupported_batch_count",
          stats.NativePicaTexture2BackendUnsupportedBatchCount },
        { "shader_input_layout_mismatch_count", stats.ShaderInputLayoutMismatchCount },
        { "native_blend_state_batch_count", stats.NativeBlendStateBatchCount },
        { "native_blend_state_applied_batch_count", stats.NativeBlendStateAppliedBatchCount },
        { "native_blend_state_unsupported_batch_count", stats.NativeBlendStateUnsupportedBatchCount },
        { "native_blend_state_backend_applied_batch_count",
          stats.NativeBlendStateBackendAppliedBatchCount },
        { "native_blend_state_backend_unsupported_batch_count",
          stats.NativeBlendStateBackendUnsupportedBatchCount },
        { "native_cull_state_batch_count", stats.NativeCullStateBatchCount },
        { "native_cull_state_applied_batch_count", stats.NativeCullStateAppliedBatchCount },
        { "native_cull_state_backend_unsupported_batch_count",
          stats.NativeCullStateBackendUnsupportedBatchCount },
        { "native_sampler_state_application_count", stats.NativeSamplerStateApplicationCount },
        { "native_sampler_state_backend_applied_count", stats.NativeSamplerStateBackendAppliedCount },
        { "native_sampler_state_backend_unsupported_count",
          stats.NativeSamplerStateBackendUnsupportedCount },
        { "native_pica_alpha_test_decoded_batch_count",
          stats.NativePicaAlphaTestDecodedBatchCount },
        { "native_pica_alpha_test_applied_batch_count",
          stats.NativePicaAlphaTestAppliedBatchCount },
        { "native_pica_alpha_test_backend_unsupported_batch_count",
          stats.NativePicaAlphaTestBackendUnsupportedBatchCount },
        { "native_pica_material_lut_input_packet_available_batch_count",
          stats.NativePicaMaterialLutInputPacketAvailableBatchCount },
        { "native_pica_material_lut_input_packet_complete_batch_count",
          stats.NativePicaMaterialLutInputPacketCompleteBatchCount },
        { "native_pica_material_lut_input_fragment_lighting_batch_count",
          stats.NativePicaMaterialLutInputFragmentLightingBatchCount },
        { "native_pica_material_lut_input_evaluation_pending_batch_count",
          stats.NativePicaMaterialLutInputEvaluationPendingBatchCount },
        { "native_pica_material_lut_input_evaluation_applied_batch_count",
          stats.NativePicaMaterialLutInputEvaluationAppliedBatchCount },
        { "native_pica_fog_available", stats.NativePicaFogAvailable },
        { "native_pica_fog_used_for_render", stats.NativePicaFogUsedForRender },
        { "native_pica_fog_shader_supported", stats.NativePicaFogShaderSupported },
        { "native_pica_fog_lut_shader_supported", stats.NativePicaFogLutShaderSupported },
        { "native_pica_fog_fragment_lut_backend_supported",
          stats.NativePicaFogFragmentLutBackendSupported },
        { "native_pica_fog_enabled", stats.NativePicaFogEnabled },
        { "native_pica_fog_source_kind", stats.NativePicaFogSourceKind },
        { "native_pica_fog_blocked_reason", stats.NativePicaFogBlockedReason },
        { "native_pica_fog_color",
          {
              { "r", static_cast<int>(stats.NativePicaFogColor.R) },
              { "g", static_cast<int>(stats.NativePicaFogColor.G) },
              { "b", static_cast<int>(stats.NativePicaFogColor.B) },
              { "a", static_cast<int>(stats.NativePicaFogColor.A) },
          } },
        { "native_pica_fog_mode", stats.NativePicaFogMode },
        { "native_pica_fog_lut_word_count", stats.NativePicaFogLutWordCount },
        { "native_pica_fog_lut_words", stats.NativePicaFogLutWords },
        { "native_pica_fog_fragment_parameter_bind_batch_count",
          stats.NativePicaFogFragmentParameterBindBatchCount },
        { "native_pica_fog_fragment_parameter_bind_failure_batch_count",
          stats.NativePicaFogFragmentParameterBindFailureBatchCount },
        { "native_pica_fog_decoded_batch_count", stats.NativePicaFogDecodedBatchCount },
        { "native_pica_fog_applied_batch_count", stats.NativePicaFogAppliedBatchCount },
        { "native_pica_fog_pending_batch_count", stats.NativePicaFogPendingBatchCount },
        { "native_pica_self_shadow_route_available", stats.NativePicaSelfShadowRouteAvailable },
        { "native_pica_self_shadow_shader_route_decoded",
          stats.NativePicaSelfShadowShaderRouteDecoded },
        { "native_pica_self_shadow_shader_route_pending",
          stats.NativePicaSelfShadowShaderRoutePending },
        { "native_pica_self_shadow_shader_equation_semantics_supported",
          stats.NativePicaSelfShadowShaderEquationSemanticsSupported },
        { "native_pica_self_shadow_texture_sampling_semantics_supported",
          stats.NativePicaSelfShadowTextureSamplingSemanticsSupported },
        { "native_pica_self_shadow_full_primary_light_contribution_supported",
          stats.NativePicaSelfShadowFullPrimaryLightContributionSupported },
        { "native_pica_shadow2d_texture_type_supported",
          stats.NativePicaShadow2dTextureTypeSupported },
        { "native_pica_shadow2d_backend_pass_request_supported",
          stats.NativePicaShadow2dBackendPassRequestSupported },
        { "native_pica_shadow2d_backend_pass_requested",
          stats.NativePicaShadow2dBackendPassRequested },
        { "native_pica_shadow2d_backend_pass_pending",
          stats.NativePicaShadow2dBackendPassPending },
        { "native_pica_shadow2d_visual_pass_request_supported",
          stats.NativePicaShadow2dVisualPassRequestSupported },
        { "native_pica_shadow2d_visual_pass_requested",
          stats.NativePicaShadow2dVisualPassRequested },
        { "native_pica_shadow2d_visual_pass_pending",
          stats.NativePicaShadow2dVisualPassPending },
        { "native_pica_shadow2d_material_texture_projection_input_supported",
          stats.NativePicaShadow2dMaterialTextureProjectionInputSupported },
        { "native_pica_shadow2d_texcoord0_w_input_supported",
          stats.NativePicaShadow2dTexCoord0WInputSupported },
        { "native_pica_shadow2d_encoded_depth_compare_supported",
          stats.NativePicaShadow2dEncodedDepthCompareSupported },
        { "native_pica_shadow2d_projection_register_values_decoded",
          stats.NativePicaShadow2dProjectionRegisterValuesDecoded },
        { "native_pica_shadow2d_projection_register_trace_available",
          stats.NativePicaShadow2dProjectionRegisterTraceAvailable },
        { "native_pica_shadow2d_dmp_shadow_z_uniforms_decoded",
          stats.NativePicaShadow2dDmpShadowZUniformsDecoded },
        { "native_pica_shadow2d_pica_texture_shadow_register_decoded",
          stats.NativePicaShadow2dPicaTextureShadowRegisterDecoded },
        { "native_pica_shadow2d_pica_framebuffer_shadow_register_decoded",
          stats.NativePicaShadow2dPicaFramebufferShadowRegisterDecoded },
        { "native_pica_shadow2d_shader_route_register_trace_decoded",
          stats.NativePicaShadow2dShaderRouteRegisterTraceDecoded },
        { "native_pica_shadow2d_shader_route_matches_primary_rgb_shadow_term",
          stats.NativePicaShadow2dShaderRouteMatchesPrimaryRgbShadowTerm },
        { "native_pica_shadow2d_shader_route_trace_disables_primary_rgb_shadow_term",
          stats.NativePicaShadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm },
        { "native_pica_shadow2d_shadow_texture_dim_decoded",
          stats.NativePicaShadow2dShadowTextureDimDecoded },
        { "native_pica_shadow2d_backend_shadow_map_render_target_decoded",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetDecoded },
        { "native_pica_shadow2d_backend_shadow_map_render_target_requested",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetRequested },
        { "native_pica_shadow2d_backend_shadow_map_render_target_allocated",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetAllocated },
        { "native_pica_shadow2d_backend_shadow_map_render_target_native_format_supported",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetNativeFormatSupported },
        { "native_pica_shadow2d_backend_r32ui_pipeline_supported",
          stats.NativePicaShadow2dBackendR32uiPipelineSupported },
        { "native_pica_shadow2d_backend_depth_encode_shader_supported",
          stats.NativePicaShadow2dBackendDepthEncodeShaderSupported },
        { "native_pica_shadow2d_primary_rgb_sample_compare_shader_supported",
          stats.NativePicaShadow2dPrimaryRgbSampleCompareShaderSupported },
        { "native_pica_shadow2d_visual_pass_ready",
          stats.NativePicaShadow2dVisualPassReady },
        { "native_pica_shadow2d_visual_pass_uses_runtime_n64_asset_substitution",
          stats.NativePicaShadow2dVisualPassUsesRuntimeN64AssetSubstitution },
        { "native_pica_self_shadow_uses_runtime_n64_asset_substitution",
          stats.NativePicaSelfShadowUsesRuntimeN64AssetSubstitution },
        { "native_pica_self_shadow_light_contribution_formula",
          stats.NativePicaSelfShadowLightContributionFormula },
        { "native_pica_shadow2d_backend_pass_request_source",
          stats.NativePicaShadow2dBackendPassRequestSource },
        { "native_pica_shadow2d_visual_pass_source_kind",
          stats.NativePicaShadow2dVisualPassSourceKind },
        { "native_pica_shadow2d_visual_pass_render_target_format",
          stats.NativePicaShadow2dVisualPassRenderTargetFormat },
        { "native_pica_shadow2d_material_texture_projection_input_source",
          stats.NativePicaShadow2dMaterialTextureProjectionInputSource },
        { "native_pica_shadow2d_texcoord0_w_input_source",
          stats.NativePicaShadow2dTexCoord0WInputSource },
        { "native_pica_shadow2d_projection_register_value_source",
          stats.NativePicaShadow2dProjectionRegisterValueSource },
        { "native_pica_shadow2d_projection_register_trace_source_kind",
          stats.NativePicaShadow2dProjectionRegisterTraceSourceKind },
        { "native_pica_shadow2d_projection_register_trace_format",
          stats.NativePicaShadow2dProjectionRegisterTraceFormat },
        { "native_pica_shadow2d_projection_register_decode_source",
          stats.NativePicaShadow2dProjectionRegisterDecodeSource },
        { "native_pica_shadow2d_shader_route_trace_status",
          stats.NativePicaShadow2dShaderRouteTraceStatus },
        { "native_pica_shadow2d_backend_shadow_map_render_target_source",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetSource },
        { "native_pica_shadow2d_backend_shadow_map_render_target_blocked_reason",
          stats.NativePicaShadow2dBackendShadowMapRenderTargetBlockedReason },
        { "native_pica_shadow2d_visual_pass_application",
          stats.NativePicaShadow2dVisualPassApplication },
        { "native_pica_shadow2d_visual_pass_blocked_reason",
          stats.NativePicaShadow2dVisualPassBlockedReason },
        { "native_pica_shadow2d_shadow_map_format",
          stats.NativePicaShadow2dShadowMapFormat },
        { "native_pica_shadow2d_compare_source",
          stats.NativePicaShadow2dCompareSource },
        { "native_pica_shadow2d_filter",
          stats.NativePicaShadow2dFilter },
        { "native_pica_shadow2d_z_formula",
          stats.NativePicaShadow2dZFormula },
        { "native_pica_shadow2d_encoded_depth_decode_source",
          stats.NativePicaShadow2dEncodedDepthDecodeSource },
        { "native_pica_shadow2d_encoded_depth_bits",
          stats.NativePicaShadow2dEncodedDepthBits },
        { "native_pica_shadow2d_encoded_alpha_bits",
          stats.NativePicaShadow2dEncodedAlphaBits },
        { "native_pica_shadow2d_bias_shift",
          stats.NativePicaShadow2dBiasShift },
        { "native_pica_shadow2d_filter_tap_count",
          stats.NativePicaShadow2dFilterTapCount },
        { "native_pica_shadow2d_filter_result_channel_count",
          stats.NativePicaShadow2dFilterResultChannelCount },
        { "native_pica_shadow2d_texture_shadow_compare_bias",
          stats.NativePicaShadow2dTextureShadowCompareBias },
        { "native_pica_shadow2d_shadow_texture_dim_register_index",
          stats.NativePicaShadow2dShadowTextureDimRegisterIndex },
        { "native_pica_shadow2d_shadow_texture_dim_raw",
          stats.NativePicaShadow2dShadowTextureDimRaw },
        { "native_pica_shadow2d_shadow_texture_width",
          stats.NativePicaShadow2dShadowTextureWidth },
        { "native_pica_shadow2d_shadow_texture_height",
          stats.NativePicaShadow2dShadowTextureHeight },
        { "native_pica_shadow2d_backend_shadow_map_framebuffer_id",
          stats.NativePicaShadow2dBackendShadowMapFramebufferId },
        { "native_pica_shadow2d_texture_shadow_orthographic",
          stats.NativePicaShadow2dTextureShadowOrthographic },
        { "native_pica_shadow2d_dmp_shadow_z_bias",
          stats.NativePicaShadow2dDmpShadowZBias },
        { "native_pica_shadow2d_dmp_shadow_z_scale",
          stats.NativePicaShadow2dDmpShadowZScale },
        { "native_pica_shadow2d_framebuffer_shadow_constant",
          stats.NativePicaShadow2dFramebufferShadowConstant },
        { "native_pica_shadow2d_framebuffer_shadow_linear",
          stats.NativePicaShadow2dFramebufferShadowLinear },
        { "native_pica_shadow2d_out_of_bounds_result",
          stats.NativePicaShadow2dOutOfBoundsResult },
        { "native_pica_shadow2d_filter_interpolation_source",
          stats.NativePicaShadow2dFilterInterpolationSource },
        { "native_pica_self_shadowed_light_register_count",
          stats.NativePicaSelfShadowedLightRegisterCount },
        { "native_pica_self_shadow_candidate_batch_count",
          stats.NativePicaSelfShadowCandidateBatchCount },
        { "native_pica_self_shadow_candidate_vertex_count",
          stats.NativePicaSelfShadowCandidateVertexCount },
        { "native_pica_shadow2d_material_texture_projection_decoded_batch_count",
          stats.NativePicaShadow2dMaterialTextureProjectionDecodedBatchCount },
        { "native_pica_shadow2d_texcoord0_w_input_decoded_batch_count",
          stats.NativePicaShadow2dTexCoord0WInputDecodedBatchCount },
        { "native_pica_self_shadow_pending_batch_count",
          stats.NativePicaSelfShadowPendingBatchCount },
        { "native_pica_self_shadow_applied_batch_count",
          stats.NativePicaSelfShadowAppliedBatchCount },
        { "native_kankyo_primitive_backend_input_submit_count",
          stats.NativeKankyoPrimitiveBackendInputSubmitCount },
        { "native_kankyo_primitive_decoded_texture_submit_count",
          stats.NativeKankyoPrimitiveDecodedTextureSubmitCount },
        { "native_kankyo_primitive_missing_texture_submit_count",
          stats.NativeKankyoPrimitiveMissingTextureSubmitCount },
        { "native_kankyo_primitive_overlay_vertex_input_count",
          stats.NativeKankyoPrimitiveOverlayVertexInputCount },
        { "native_kankyo_primitive_expected_expanded_vertex_count",
          stats.NativeKankyoPrimitiveExpectedExpandedVertexCount },
        { "native_kankyo_primitive_runtime28c_quad_lane_vertex_input_count",
          stats.NativeKankyoPrimitiveRuntime28CQuadLaneVertexInputCount },
        { "native_kankyo_primitive_runtime28c_draw_count",
          stats.NativeKankyoPrimitiveRuntime28CDrawCount },
        { "native_kankyo_primitive_visible_draw_call_count",
          stats.NativeKankyoPrimitiveVisibleDrawCallCount },
        { "native_kankyo_primitive_visible_triangle_count",
          stats.NativeKankyoPrimitiveVisibleTriangleCount },
        { "native_kankyo_primitive_visible_vertex_count",
          stats.NativeKankyoPrimitiveVisibleVertexCount },
        { "native_kankyo_primitive_color_pass_primitive_value",
          stats.NativeKankyoPrimitiveColorPassPrimitiveValue },
        { "native_kankyo_primitive_alpha_pass_primitive_value",
          stats.NativeKankyoPrimitiveAlphaPassPrimitiveValue },
        { "native_kankyo_primitive_texture_name",
          stats.NativeKankyoPrimitiveTextureName },
        { "native_kankyo_primitive_terminal_texture_name",
          stats.NativeKankyoPrimitiveTerminalTextureName },
        { "native_kankyo_primitive_visibility_target",
          stats.NativeKankyoPrimitiveVisibilityTarget },
        { "native_kankyo_primitive_visibility_factor",
          stats.NativeKankyoPrimitiveVisibilityFactor },
        { "native_kankyo_primitive_visibility_screen_gate_resolved",
          stats.NativeKankyoPrimitiveVisibilityScreenGateResolved },
        { "native_kankyo_primitive_visibility_scene_occlusion_resolved",
          stats.NativeKankyoPrimitiveVisibilitySceneOcclusionResolved },
        { "native_kankyo_primitive_visibility_source_in_viewport",
          stats.NativeKankyoPrimitiveVisibilitySourceInViewport },
        { "native_kankyo_primitive_visibility_source_occluded",
          stats.NativeKankyoPrimitiveVisibilitySourceOccluded },
        { "native_kankyo_primitive_visibility_suppressed_draw_count",
          stats.NativeKankyoPrimitiveVisibilitySuppressedDrawCount },
        { "native_kankyo_primitive_texture_has_native_alpha",
          stats.NativeKankyoPrimitiveTextureHasNativeAlpha },
        { "native_kankyo_primitive_pica_alpha_blend_semantics_required",
          stats.NativeKankyoPrimitivePicaAlphaBlendSemanticsRequired },
        { "native_kankyo_primitive_pica_alpha_blend_semantics_resolved",
          stats.NativeKankyoPrimitivePicaAlphaBlendSemanticsResolved },
        { "native_kankyo_primitive_backend_input_resolved",
          stats.NativeKankyoPrimitiveBackendInputResolved },
        { "native_kankyo_primitive_runtime28c_quad_lane_materialized",
          stats.NativeKankyoPrimitiveRuntime28CQuadLaneMaterialized },
        { "native_kankyo_primitive_runtime28c_position_producer_resolved",
          stats.NativeKankyoPrimitiveRuntime28CPositionProducerResolved },
        { "native_kankyo_primitive_runtime28c_position_runtime_input_resolved",
          stats.NativeKankyoPrimitiveRuntime28CPositionRuntimeInputResolved },
        { "native_kankyo_primitive_pica_fog_decoded",
          stats.NativeKankyoPrimitivePicaFogDecoded },
        { "native_kankyo_primitive_pica_fog_applied",
          stats.NativeKankyoPrimitivePicaFogApplied },
        { "native_kankyo_primitive_pica_fog_pending",
          stats.NativeKankyoPrimitivePicaFogPending },
        { "native_kankyo_primitive_visible_draw_pending",
          stats.NativeKankyoPrimitiveVisibleDrawPending },
        { "native_kankyo_primitive_visible_draw_blocked_reason",
          stats.NativeKankyoPrimitiveVisibleDrawBlockedReason },
    };
}

} // namespace ThreeDsRecomp::Oot3d
