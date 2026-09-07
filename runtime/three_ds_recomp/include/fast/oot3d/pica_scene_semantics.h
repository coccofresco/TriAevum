#pragma once

#include "fast/renderer3ds/pica_scene_semantics.h"

#include <array>
#include <cstdint>
#include <span>

namespace Fast::Oot3d {

using ::Fast::Renderer3ds::kPicaNativeFogLutEntryCount;
using ::Fast::Renderer3ds::kPicaNativeLightCount;
using ::Fast::Renderer3ds::PicaNativeDepthState;
using ::Fast::Renderer3ds::PicaNativeDrawEnvironment;
using ::Fast::Renderer3ds::PicaNativeFogState;
using ::Fast::Renderer3ds::PicaNativeFragmentLightingState;
using ::Fast::Renderer3ds::PicaNativeFragmentLightState;
using ::Fast::Renderer3ds::PicaNativeLightingState;
using ::Fast::Renderer3ds::PicaNativeLightState;
using ::Fast::Renderer3ds::PicaNativeMatrix4;
using ::Fast::Renderer3ds::PicaNativeSkeletonState;
using ::Fast::Renderer3ds::PicaNativeTransformState;
using ::Fast::Renderer3ds::PicaNativeVertexState;

[[nodiscard]] uint64_t HashPicaUniformBytes(
    std::span<const uint8_t> bytes) noexcept;

[[nodiscard]] PicaNativeDrawEnvironment DecodePicaNativeDrawEnvironment(
    std::span<const uint8_t> packedVertexUniforms,
    std::span<const uint8_t> packedFragmentUniforms,
    const ::Fast::Renderer3ds::PicaFragmentFeatureView&
        fragmentFeatures) noexcept;

[[nodiscard]] PicaNativeVertexState DecodePicaNativeVertexState(
    std::span<const uint8_t> packedVertexUniforms,
    std::span<const uint8_t> packedPreviousVertexUniforms,
    bool previousVertexUniformsAvailable,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks) noexcept;

[[nodiscard]] bool DecodePicaNativeFogLut(
    std::span<const uint8_t> packedFragmentUniforms,
    const PicaNativeFogState& fog,
    std::span<std::array<float, 2>> output) noexcept;

} // namespace Fast::Oot3d
