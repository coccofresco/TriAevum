#pragma once

#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Oot3d {

enum class PicaGrassTextureCoordinateEligibility : uint8_t {
    Applied,
    UnsupportedVertexProgram,
    UnsupportedFragmentSampling,
    MissingUniforms,
    TextureCoordinatesDisabled,
    ProceduralCoordinates,
    UnsupportedCoordinateSource,
    InvalidTransform,
};

[[nodiscard]] const char* PicaGrassTextureCoordinateEligibilityName(
    PicaGrassTextureCoordinateEligibility eligibility) noexcept;

struct PicaGrassTextureCoordinateTransform {
    // Two affine rows mapping the raw CMB UV attribute to the exact texture
    // coordinates consumed by PICA texture unit zero.
    std::array<float, 6> RawToSample{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F};
    PicaGrassTextureCoordinateEligibility Eligibility =
        PicaGrassTextureCoordinateEligibility::UnsupportedVertexProgram;

    [[nodiscard]] bool Applied() const noexcept {
        return Eligibility ==
            PicaGrassTextureCoordinateEligibility::Applied;
    }
};

// Decodes the common OoT3D CMB texture-coordinate path from the same PICA
// program and uniform payload used for the draw. Unsupported procedural or
// projective paths are declined so grass is never placed with guessed UVs.
[[nodiscard]] PicaGrassTextureCoordinateTransform
DecodePicaGrassTextureCoordinateTransform(
    std::string_view vertexShaderSource,
    std::string_view fragmentShaderSource,
    std::span<const uint8_t> vertexUniformBytes,
    uint8_t texCoord0InputLocation,
    const ::Oot3d::Renderer::PicaVertexShaderHookLayout*
        vertexShaderHooks = nullptr,
    const ::Oot3d::Renderer::PicaShaderHookLayout*
        fragmentShaderHooks = nullptr) noexcept;

[[nodiscard]] std::array<float, 2>
ApplyPicaGrassTextureCoordinateTransform(
    const PicaGrassTextureCoordinateTransform& transform,
    const std::array<float, 2>& rawUv) noexcept;

[[nodiscard]] uint64_t
PicaGrassTextureCoordinateTransformVersion(
    const PicaGrassTextureCoordinateTransform& transform) noexcept;

} // namespace Fast::Oot3d
