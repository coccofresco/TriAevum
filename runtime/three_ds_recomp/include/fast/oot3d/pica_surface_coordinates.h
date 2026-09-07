#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Fast::Oot3d {

// Color, depth and MRT guides share physical PICA texel coordinates. Their
// normals remain in the unrotated camera basis; storage rotation is not a
// rotation of the lighting coordinate system.
struct PicaSurfaceCoordinates {
    bool FlipY = false;
    constexpr uint32_t ShaderFlags() const noexcept { return 1U | (FlipY ? 2U : 0U); }

    static constexpr PicaSurfaceCoordinates FromTransferFlags(uint32_t flags) noexcept {
        const bool linear = (flags & (1U << 1U)) != 0U;
        const bool dontSwizzle = (flags & (1U << 5U)) != 0U;
        return {((!linear) != (linear != dontSwizzle)) != ((flags & 1U) != 0U)};
    }

    constexpr std::array<float, 2> PresentationToStorage(std::array<float, 2> uv) const noexcept {
        return {1.0F - uv[1], FlipY ? 1.0F - uv[0] : uv[0]};
    }

    constexpr std::array<float, 2> StorageToViewNdc(std::array<float, 2> uv) const noexcept {
        return {(FlipY ? 1.0F - uv[1] : uv[1]) * 2.0F - 1.0F, uv[0] * 2.0F - 1.0F};
    }

    // FidelityFX projects to UV with a downward image Y. Rotate clip XY,
    // retaining the original depth, W and the unrotated view-space basis.
    constexpr std::array<float, 16> FidelityFxClip(std::array<float, 16> matrix) const noexcept {
        for (size_t column = 0; column < 4; ++column) {
            const float x = matrix[column * 4];
            matrix[column * 4] = matrix[column * 4 + 1];
            matrix[column * 4 + 1] = FlipY ? x : -x;
        }
        return matrix;
    }

    // CACAO only reads diagonal FOV terms: express its camera in the storage
    // basis (X = view Y, Y = +/-view X), then use positive-depth Z.
    constexpr std::array<float, 16> CacaoProjection(std::array<float, 16> projection) const noexcept {
        const float x = projection[0];
        projection[0] = projection[5];
        projection[5] = x;
        return projection;
    }

    constexpr std::array<float, 16> CacaoNormalTransform(float clipWFromViewZ) const noexcept {
        const float z = clipWFromViewZ < 0.0F ? -1.0F : 1.0F;
        // FFX uploads these bytes to a column-major HLSL matrix and evaluates
        // mul(normal, matrix). Match that memory layout, not a CPU row matrix.
        return {0, 1, 0, 0,
                FlipY ? 1.0F : -1.0F, 0, 0, 0,
                0, 0, z, 0,
                0, 0, 0, 1};
    }
};

inline constexpr std::string_view EffectSurfaceCoordinateShaderLibrary = R"glsl(
vec2 oot3d_surface_view_ndc(vec2 uv, uint orientation) {
    if ((orientation & 1u) == 0u) return uv * 2.0 - 1.0;
    float x = (orientation & 2u) != 0u ? 1.0 - uv.y : uv.y;
    return vec2(x, uv.x) * 2.0 - 1.0;
}
vec2 oot3d_view_ndc_surface_uv(vec2 ndc, uint orientation) {
    vec2 uv = ndc * 0.5 + 0.5;
    if ((orientation & 1u) == 0u) return uv;
    return vec2(uv.y, (orientation & 2u) != 0u ? 1.0 - uv.x : uv.x);
}
)glsl";

} // namespace Fast::Oot3d
