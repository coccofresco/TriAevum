#pragma once

#include "fast/renderer3ds/pica_native_state.h"

#include <cstdint>

namespace Fast::Renderer3ds {

// Whether a native world draw's colour is surface illumination the per-pixel
// model may replace, or authored appearance it must keep. Typed native state
// only (docs/TRIAEVUM_SURFACE_LIGHTING_OWNERSHIP.md): no texture, material,
// scene or screen heuristics.
//
// - Additive composition (RGB equation Add, destination factor One) adds
//   light to what is behind it: flames, glows, fake light shafts, lit windows.
//   Relighting it as a surface removes the light it was authored to add.
// - A draw the title lights neither per vertex nor per fragment has a colour
//   no light produced (effects, smoke, particles); replacing that colour with
//   reconstructed illumination discards it. World surfaces, rooms included,
//   are vertex-lit in the titles measured.
//
// Kept appearance means native colour, alpha, blending, fog and order: no
// per-pixel/PBR replacement and no received directional or point shadows.
enum class PicaSurfaceAppearance : uint8_t {
    Relightable = 0,
    AdditiveComposition = 1,
    Unlit = 2,
};

[[nodiscard]] constexpr PicaSurfaceAppearance ClassifyPicaSurfaceAppearance(
    const NativeBlendState& blend, bool vertexLit, bool fragmentLit) noexcept {
    if (blend.Enabled && blend.EquationRgb == NativeBlendEquation::Add && blend.DestRgb == NativeBlendFactor::One)
        return PicaSurfaceAppearance::AdditiveComposition;
    if (!vertexLit && !fragmentLit) return PicaSurfaceAppearance::Unlit;
    return PicaSurfaceAppearance::Relightable;
}

} // namespace Fast::Renderer3ds
