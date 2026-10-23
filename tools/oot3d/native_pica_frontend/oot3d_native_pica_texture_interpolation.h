#pragma once

#include "oot3d_native_pica_affine_layout.h"
#include "fast/renderer3ds/pica_texture_interpolation.h"

namespace Oot3dNativeGame {

// A source-fingerprinted CMB coordinate output with its canonical S/T mapping.
// Kept on the cached temporal program; dynamic branch/wrap state is NOT cached.
using Oot3dCmbTextureCoordinates = std::array<bool, 3>;

inline Oot3dCmbTextureCoordinates DescribeOot3dCmbTextureCoordinates(
    const Oot3dPicaDrawPacket& packet, const Oot3dPicaDecodedDrawState& state) {
    Oot3dCmbTextureCoordinates result{};
    if (DescribeOot3dAffineUniforms(packet, state) != kOot3dCmbAffineLayout) return result;
    // Match the generator's effective semantic map (including last assignment).
    std::array<std::array<uint8_t, 2>, 24> sources;
    for (auto& source : sources) source = {0xffU, 0xffU};
    size_t packed = 0;
    for (uint8_t reg = 0; reg < 16U; ++reg) {
        if (!(state.ShaderInterface.OutputMask & (1U << reg))) continue;
        if (packed < 7U) for (uint8_t component = 0; component < 4U; ++component) {
            const auto semantic = (packet.Registers[0x50U + packed] >> (8U * component)) & 0x1fU;
            if (semantic < sources.size()) sources[semantic] = {reg, component};
        }
        ++packed;
    }
    constexpr std::array<uint8_t, 3> firstSemantic{12U, 14U, 22U};
    for (uint8_t coordinate = 0; coordinate < result.size(); ++coordinate) {
        const uint8_t reg = 2U + coordinate, first = firstSemantic[coordinate];
        result[coordinate] = sources[first] == std::array<uint8_t, 2>{reg, 0U} &&
                             sources[first + 1U] == std::array<uint8_t, 2>{reg, 1U};
        // A texture output reused as color/position/another coordinate is an
        // additional unproven consumer. Do not change its uniform translation.
        for (size_t semantic = 0; semantic < sources.size(); ++semantic)
            if (sources[semantic][0] == reg && sources[semantic][1] < 2U &&
                semantic != size_t(first) + sources[semantic][1]) result[coordinate] = false;
    }
    return result;
}

inline Fast::Renderer3ds::PicaTextureTranslations DescribeOot3dTextureTranslations(
    const Oot3dCmbTextureCoordinates& proven,
    std::span<const std::array<float, 4>> floats, uint32_t booleans,
    const Oot3dPicaDecodedDrawState& state, uint32_t textureControl) {
    using namespace Fast::Renderer3ds;
    PicaTextureTranslations result{};
    // Procedural texture evaluation is another consumer of the coordinates;
    // ordinary texture wrapping does not prove its periodicity.
    if (floats.size() < 96U || !(booleans & 2U) || (textureControl & (1U << 10U)) ||
        state.ShaderInterface.GeometryShaderEnabled ||
        floats[93] != std::array<float, 4>{0, 1, 2, 3}) return result;
    // Refuse the entire description if any enabled texture has an unproven
    // sampling operation; do not hide a possible aliased consumer.
    if (std::any_of(state.Textures.begin(), state.Textures.end(),
                    [](const auto& texture) { return texture.Enabled && texture.Type != 0U; })) return result;
    const float mode = floats[89][3];
    if (mode != 0.0F && mode != 1.0F && mode != 2.0F) return result;
    const bool board = mode == 2.0F;
    constexpr std::array<uint8_t, 3> firstRow{10U, 14U, 17U};
    const auto wrap = [](uint8_t value) {
        return value == 2U ? PicaTextureWrap::Repeat :
               value == 3U ? PicaTextureWrap::MirroredRepeat : PicaTextureWrap::NonPeriodic;
    };
    for (uint8_t unit = 0; unit < state.Textures.size(); ++unit) {
        const auto& texture = state.Textures[unit];
        if (!texture.Enabled) continue;
        const uint8_t coordinate = unit == 2U && state.Texture2UsesCoordinate1 ? 1U : unit;
        if (!proven[coordinate]) continue;
        // Model branch: mode 3 generates coordinates from normals, mode 4
        // from inverse-view positions. Board branch ignores c92 entirely and
        // ALWAYS calls the UV resolver. This is the captured cloud draw.
        if (!board && (floats[95][0] != 3.0F || floats[95][1] != 4.0F ||
            !std::isfinite(floats[92][coordinate]) || floats[92][coordinate] == 3.0F ||
            floats[92][coordinate] == 4.0F)) continue;
        const float selector = floats[89][coordinate];
        if (selector != 0.0F && selector != 1.0F && selector != 2.0F) continue;
        const auto source = static_cast<uint8_t>(selector);
        if (!(booleans & (1U << (6U + source))) || !std::isfinite(floats[91][source])) continue;
        for (uint8_t axis = 0; axis < 2U; ++axis) {
            const uint8_t row = firstRow[coordinate] + axis;
            if (!std::all_of(floats[row].begin(), floats[row].end(),
                             [](float value) { return std::isfinite(value); })) continue;
            auto& translation = result[2U * unit + axis];
            translation.UniformRow = row;
            translation.CoordinateSet = coordinate;
            translation.InputRegister = 3U + source;
            translation.Wrap = wrap(axis == 0U ? texture.WrapS : texture.WrapT);
            translation.CoordinateScale = axis == 0U ? 1.0F : -1.0F;
            // UV resolver (CMB words 280..281 / 285..286 / 289..290):
            // r10 = (attribute.xy * c91[source], 1, 1).
            // DP4 -> final S/T, followed only by the fragment's T = 1 - T.
            // c[row].w is therefore a POST-linear translation in UV units.
            // Non-unit, rotated or animated row.xy and input scale do NOT
            // divide its period. row.z is the other additive constant; include
            // its change when unwrapping the total translation via row.w.
            translation.CoordinateBias = translation.CoordinateScale * double(floats[row][2]) +
                                         (axis == 0U ? 0.0 : 1.0);
        }
    }
    return result;
}

} // namespace Oot3dNativeGame
