#pragma once

#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

inline constexpr std::size_t kPicaPackedVec4Bytes = 4U * sizeof(float);

inline constexpr std::size_t kPicaPackedVertexBooleanMaskOffset = 0U;
inline constexpr std::size_t kPicaPackedVertexFlipViewportOffset =
    sizeof(std::uint32_t);
inline constexpr std::size_t kPicaPackedVertexIntegerOffset = 16U;
inline constexpr std::size_t kPicaPackedVertexFloatOffset =
    kPicaPackedVertexIntegerOffset + 4U * kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedVertexUniformSize =
    kPicaPackedVertexFloatOffset + 96U * kPicaPackedVec4Bytes;

inline constexpr std::size_t kPicaPackedFragmentTevConstantsOffset = 0U;
inline constexpr std::size_t kPicaPackedFragmentCombinerBufferOffset =
    kPicaPackedFragmentTevConstantsOffset + 6U * kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentAlphaReferenceOffset =
    kPicaPackedFragmentCombinerBufferOffset + kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentDepthScaleOffset =
    kPicaPackedFragmentAlphaReferenceOffset + sizeof(std::int32_t);
inline constexpr std::size_t kPicaPackedFragmentDepthOffsetOffset =
    kPicaPackedFragmentDepthScaleOffset + sizeof(float);
inline constexpr std::size_t kPicaPackedFragmentWBufferingOffset =
    kPicaPackedFragmentDepthOffsetOffset + sizeof(float);
inline constexpr std::size_t kPicaPackedFragmentFogColorOffset = 128U;
inline constexpr std::size_t kPicaPackedFragmentFogLutOffset =
    kPicaPackedFragmentFogColorOffset + kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentTextureLodBiasOffset =
    kPicaPackedFragmentFogLutOffset + 64U * kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentBaseUniformSize =
    kPicaPackedFragmentTextureLodBiasOffset + kPicaPackedVec4Bytes;

inline constexpr std::size_t kPicaPackedFragmentLightingArrayBytes =
    8U * kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentLightingSpecular0Offset =
    kPicaPackedFragmentBaseUniformSize;
inline constexpr std::size_t kPicaPackedFragmentLightingSpecular1Offset =
    kPicaPackedFragmentLightingSpecular0Offset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingDiffuseOffset =
    kPicaPackedFragmentLightingSpecular1Offset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingAmbientOffset =
    kPicaPackedFragmentLightingDiffuseOffset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingPositionOffset =
    kPicaPackedFragmentLightingAmbientOffset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingSpotDirectionOffset =
    kPicaPackedFragmentLightingPositionOffset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingAttenuationOffset =
    kPicaPackedFragmentLightingSpotDirectionOffset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentLightingGlobalAmbientOffset =
    kPicaPackedFragmentLightingAttenuationOffset +
    kPicaPackedFragmentLightingArrayBytes;
inline constexpr std::size_t kPicaPackedFragmentShadowTextureBiasOffset =
    kPicaPackedFragmentLightingGlobalAmbientOffset + kPicaPackedVec4Bytes;
inline constexpr std::size_t kPicaPackedFragmentShadowOrthographicOffset =
    kPicaPackedFragmentShadowTextureBiasOffset + sizeof(std::int32_t);
inline constexpr std::size_t kPicaPackedFragmentShadowBiasConstantOffset =
    kPicaPackedFragmentShadowOrthographicOffset + sizeof(std::int32_t);
inline constexpr std::size_t kPicaPackedFragmentShadowBiasLinearOffset =
    kPicaPackedFragmentShadowBiasConstantOffset + sizeof(float);
inline constexpr std::size_t kPicaPackedFragmentUniformSize =
    kPicaPackedFragmentShadowBiasLinearOffset + sizeof(float);

static_assert(kPicaPackedVertexFloatOffset == 80U);
static_assert(kPicaPackedFragmentFogColorOffset == 128U);
static_assert(kPicaPackedFragmentFogLutOffset == 144U);
static_assert(kPicaPackedFragmentTextureLodBiasOffset == 1168U);
static_assert(kPicaPackedFragmentBaseUniformSize == 1184U);
static_assert(kPicaPackedFragmentLightingGlobalAmbientOffset == 2080U);
static_assert(kPicaPackedFragmentShadowTextureBiasOffset == 2096U);
static_assert(kPicaPackedFragmentShadowOrthographicOffset == 2100U);
static_assert(kPicaPackedFragmentShadowBiasConstantOffset == 2104U);
static_assert(kPicaPackedFragmentShadowBiasLinearOffset == 2108U);
static_assert(kPicaPackedFragmentUniformSize == 2112U);

} // namespace Fast::Oot3d
