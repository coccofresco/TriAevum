#include "fast/oot3d/pica_grass_texture_coordinates.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>

namespace Fast::Oot3d {
namespace {

constexpr size_t kBooleanUniformOffset = 0U;
constexpr size_t kFloatUniformOffset =
    16U + 4U * 4U * sizeof(uint32_t);
constexpr size_t kFloatUniformCount = 96U;
constexpr size_t kFloatUniformComponents = 4U;
constexpr size_t kFloatUniformBytes =
    kFloatUniformCount * kFloatUniformComponents * sizeof(float);

bool ContainsCmbUv0Contract(std::string_view source) {
    return source.find(
               "pica_output2.xy = (reg_tmp3.xyyy).xy") !=
               std::string_view::npos &&
           source.find(
               "reg_tmp3.x = dot(uniforms.f[10].xyzw, "
               "reg_tmp10.xyzw)") != std::string_view::npos &&
           source.find(
               "reg_tmp3.y = dot(uniforms.f[11].xyzw, "
               "reg_tmp10.xyzw)") != std::string_view::npos;
}

bool SamplesUv0WithNativeVFlip(
    std::string_view source,
    const ::Oot3d::Renderer::PicaShaderHookLayout* hooks) {
    if (hooks != nullptr && hooks->ValidFor(source)) {
        const auto* sample = hooks->TextureSample(0U);
        return hooks->SamplesTexture(0U) && sample != nullptr &&
               sample->Coordinate == 0U &&
               sample->Operation ==
                   ::Oot3d::Renderer::
                       PicaTextureCoordinateOperation::NativeVFlip;
    }
    return source.find("texture(pica_texture0") !=
               std::string_view::npos &&
           source.find("pica_texcoord0.x") !=
               std::string_view::npos &&
           source.find("1.0 - pica_texcoord0.y") !=
               std::string_view::npos &&
           source.find("textureProj(pica_texture0") ==
               std::string_view::npos;
}

bool Approximately(float left, float right) {
    return std::isfinite(left) && std::isfinite(right) &&
           std::abs(left - right) <= 1.0e-5F;
}

} // namespace

const char* PicaGrassTextureCoordinateEligibilityName(
    PicaGrassTextureCoordinateEligibility eligibility) noexcept {
    switch (eligibility) {
        case PicaGrassTextureCoordinateEligibility::Applied:
            return "applied";
        case PicaGrassTextureCoordinateEligibility::UnsupportedVertexProgram:
            return "unsupported vertex program";
        case PicaGrassTextureCoordinateEligibility::UnsupportedFragmentSampling:
            return "unsupported fragment sampling";
        case PicaGrassTextureCoordinateEligibility::MissingUniforms:
            return "missing uniforms";
        case PicaGrassTextureCoordinateEligibility::TextureCoordinatesDisabled:
            return "texture coordinates disabled";
        case PicaGrassTextureCoordinateEligibility::ProceduralCoordinates:
            return "procedural coordinates";
        case PicaGrassTextureCoordinateEligibility::UnsupportedCoordinateSource:
            return "unsupported coordinate source";
        case PicaGrassTextureCoordinateEligibility::InvalidTransform:
            return "invalid transform";
        default:
            return "unknown";
    }
}

PicaGrassTextureCoordinateTransform
DecodePicaGrassTextureCoordinateTransform(
    std::string_view vertexShaderSource,
    std::string_view fragmentShaderSource,
    std::span<const uint8_t> vertexUniformBytes,
    uint8_t texCoord0InputLocation,
    const ::Oot3d::Renderer::PicaVertexShaderHookLayout*
        vertexShaderHooks,
    const ::Oot3d::Renderer::PicaShaderHookLayout*
        fragmentShaderHooks) noexcept {
    PicaGrassTextureCoordinateTransform result;
    const bool typedVertexProgram =
        vertexShaderHooks != nullptr &&
        vertexShaderHooks->ValidFor(vertexShaderSource);
    const auto* vertexLayout =
        typedVertexProgram
            ? vertexShaderHooks->TextureCoordinate(0U)
            : nullptr;
    if (typedVertexProgram &&
        (!vertexShaderHooks->Has(
             ::Oot3d::Renderer::PicaVertexShaderSemantic::
                 TextureCoordinateProgram) ||
         vertexLayout == nullptr ||
         vertexLayout->Operation !=
             ::Oot3d::Renderer::
                 PicaVertexTextureCoordinateOperation::CmbAffine)) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::
                UnsupportedVertexProgram;
        return result;
    }
    if (!typedVertexProgram &&
        !ContainsCmbUv0Contract(vertexShaderSource)) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::
                UnsupportedVertexProgram;
        return result;
    }
    if (!SamplesUv0WithNativeVFlip(
            fragmentShaderSource, fragmentShaderHooks)) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::
                UnsupportedFragmentSampling;
        return result;
    }
    if (vertexUniformBytes.size() <
            kFloatUniformOffset + kFloatUniformBytes) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::MissingUniforms;
        return result;
    }

    uint32_t booleanMask = 0U;
    std::memcpy(
        &booleanMask,
        vertexUniformBytes.data() + kBooleanUniformOffset,
        sizeof(booleanMask));
    if (!typedVertexProgram && (booleanMask & 2U) == 0U) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::
                TextureCoordinatesDisabled;
        return result;
    }

    std::array<std::array<float, 4>, kFloatUniformCount> uniforms{};
    std::memcpy(
        uniforms.data(),
        vertexUniformBytes.data() + kFloatUniformOffset,
        kFloatUniformBytes);
    float inputScale = 0.0F;
    const std::array<float, 4>* rowU = nullptr;
    const std::array<float, 4>* rowV = nullptr;
    float homogeneousConstant = 0.0F;
    if (typedVertexProgram) {
        if ((booleanMask &
             (1U << vertexLayout->EnableBooleanUniform)) == 0U) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    TextureCoordinatesDisabled;
            return result;
        }
        const float coordinateMode =
            uniforms[vertexLayout->CoordinateModeUniform]
                    [vertexLayout->CoordinateModeComponent];
        const auto& modeConstants =
            uniforms[vertexLayout->CoordinateModeConstantUniform];
        if (Approximately(
                coordinateMode,
                modeConstants[vertexLayout->
                                  CoordinateModeConstantComponents[0]]) ||
            Approximately(
                coordinateMode,
                modeConstants[vertexLayout->
                                  CoordinateModeConstantComponents[1]])) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    ProceduralCoordinates;
            return result;
        }

        const float sourceSelector =
            uniforms[vertexLayout->SourceSelectorUniform]
                    [vertexLayout->SourceSelectorComponent];
        const auto& selectorConstants =
            uniforms[vertexLayout->SourceSelectorConstantUniform];
        const bool selectsFirst = Approximately(
            sourceSelector,
            selectorConstants[vertexLayout->
                                  SourceSelectorConstantComponents[0]]);
        const bool selectsSecond = Approximately(
            sourceSelector,
            selectorConstants[vertexLayout->
                                  SourceSelectorConstantComponents[1]]);
        size_t sourceIndex = 0U;
        if (selectsFirst && !selectsSecond) {
            sourceIndex = 0U;
        } else if (!selectsFirst && selectsSecond) {
            sourceIndex = 1U;
        } else if (!selectsFirst && !selectsSecond &&
                   vertexLayout->SourceCount >= 3U) {
            sourceIndex = 2U;
        } else {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    UnsupportedCoordinateSource;
            return result;
        }
        if (sourceIndex >= vertexLayout->SourceCount) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    UnsupportedCoordinateSource;
            return result;
        }
        const auto& source = vertexLayout->Sources[sourceIndex];
        if (source.InputRegister != texCoord0InputLocation ||
            (booleanMask &
             (1U << source.EnableBooleanUniform)) == 0U) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    UnsupportedCoordinateSource;
            return result;
        }
        inputScale =
            uniforms[source.ScaleUniform][source.ScaleComponent];
        rowU = &uniforms[vertexLayout->MatrixRowUUniform];
        rowV = &uniforms[vertexLayout->MatrixRowVUniform];
        homogeneousConstant =
            uniforms[vertexLayout->HomogeneousUniform]
                    [vertexLayout->HomogeneousComponent];
    } else {
        const auto& coordinateModes = uniforms[92U];
        const auto& modeConstants = uniforms[95U];
        if (Approximately(coordinateModes[0], modeConstants[0]) ||
            Approximately(coordinateModes[0], modeConstants[1])) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    ProceduralCoordinates;
            return result;
        }

        const float sourceValue = uniforms[89U][0];
        const int sourceIndex =
            static_cast<int>(std::lround(sourceValue));
        if (!Approximately(
                sourceValue, static_cast<float>(sourceIndex)) ||
            sourceIndex != 0 || texCoord0InputLocation >= 16U ||
            (booleanMask & (64U << sourceIndex)) == 0U) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    UnsupportedCoordinateSource;
            return result;
        }

        const std::string inputContract =
            "uniforms.f[91].xxxx * pica_input" +
            std::to_string(texCoord0InputLocation);
        if (vertexShaderSource.find(inputContract) ==
            std::string_view::npos) {
            result.Eligibility =
                PicaGrassTextureCoordinateEligibility::
                    UnsupportedVertexProgram;
            return result;
        }
        inputScale = uniforms[91U][0];
        rowU = &uniforms[10U];
        rowV = &uniforms[11U];
        homogeneousConstant = uniforms[93U][1];
    }
    result.RawToSample = {
        (*rowU)[0] * inputScale,
        (*rowU)[1] * inputScale,
        ((*rowU)[2] + (*rowU)[3]) * homogeneousConstant,
        -(*rowV)[0] * inputScale,
        -(*rowV)[1] * inputScale,
        1.0F -
            ((*rowV)[2] + (*rowV)[3]) * homogeneousConstant,
    };
    if (!std::isfinite(inputScale) ||
        !std::isfinite(homogeneousConstant) ||
        inputScale == 0.0F ||
        !std::all_of(
            result.RawToSample.begin(),
            result.RawToSample.end(),
            [](float value) { return std::isfinite(value); })) {
        result.Eligibility =
            PicaGrassTextureCoordinateEligibility::InvalidTransform;
        return result;
    }
    result.Eligibility =
        PicaGrassTextureCoordinateEligibility::Applied;
    return result;
}

std::array<float, 2>
ApplyPicaGrassTextureCoordinateTransform(
    const PicaGrassTextureCoordinateTransform& transform,
    const std::array<float, 2>& rawUv) noexcept {
    return {
        transform.RawToSample[0] * rawUv[0] +
            transform.RawToSample[1] * rawUv[1] +
            transform.RawToSample[2],
        transform.RawToSample[3] * rawUv[0] +
            transform.RawToSample[4] * rawUv[1] +
            transform.RawToSample[5],
    };
}

uint64_t PicaGrassTextureCoordinateTransformVersion(
    const PicaGrassTextureCoordinateTransform& transform) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (const float value : transform.RawToSample) {
        // PICA interpolation can produce either sign for an exact zero.
        // Signed zero has identical affine-transform semantics and must not
        // invalidate placement or perturb its deterministic seed.
        const uint32_t word = std::bit_cast<uint32_t>(
            value == 0.0F ? 0.0F : value);
        for (uint32_t byte = 0U; byte < sizeof(word); ++byte) {
            hash ^= static_cast<uint8_t>(
                word >> (byte * 8U));
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace Fast::Oot3d
