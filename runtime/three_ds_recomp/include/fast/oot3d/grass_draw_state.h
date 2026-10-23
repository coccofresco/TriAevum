#pragma once

#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/toon_surface_response.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

// One per grass placement: the std430 GrassEnvironmentRecord of the grass
// vertex and fragment shaders (grass_shader_sources.cpp).
struct alignas(16) GrassEnvironmentRecord {
    // W is 0 when disabled, 1 for regular depth and 2 for flipped depth.
    std::array<float, 4> ColorAndMode{};
    std::array<std::array<float, 2>, kGrassFogLutEntryCount> Lut{};
    // direction X/Z, visual seconds, strength
    std::array<float, 4> WindDirectionTime{};
    // speed, spatial scale, gust strength, gust frequency
    std::array<float, 4> WindPrimary{};
    // turbulence, per-blade randomness, maximum bend, enabled
    std::array<float, 4> WindDetail{};
    std::array<float, 4> CameraPosition{};
    std::array<float, 4> AppearanceRoot{};
    std::array<float, 4> AppearanceTip{};
    // Average texture RGB and effective texture-color influence.
    std::array<float, 4> TextureColor{};
    // Root/tip brightness, receive-lighting flag, active light count.
    std::array<float, 4> TextureBrightnessFlags{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightDirections{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightDiffuse{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightAmbient{};
    std::array<float, 4> ViewForward{};
    std::array<float, 4> ViewSide{};
    std::array<float, 4> ViewUp{};
    std::array<float, 4> BladeShape{};
    std::array<float, 4> TuftLod{};
    // Draw distance, density start/end, per-anchor fade softness.
    std::array<float, 4> DistanceLod{};
    // Tuft spread/density, final fade range, enabled.
    std::array<float, 4> TuftStyle{};
    ToonSurfaceParameters Toon;
    std::array<uint32_t, 4> NativeLighting{};
    // Rim start/end, enabled, VSM receiver strength.
    std::array<float, 4> RimDistance{};
    // The placement's previous frame, for exact motion vectors.
    std::array<float, 16> PreviousPositionToClip{};
    // Previous camera position, previous wind time.
    std::array<float, 4> PreviousCameraTime{};
    // Previous jitter NDC XY, motion enabled, previous bends bound.
    std::array<float, 4> MotionState{};
    // Card tier ends (blades, clump cards, group cards) and the blend
    // fraction (grass_card_lod.h); unused when the cards are off.
    std::array<float, 4> CardDistances{};
    // Card tile height over footprint radius per level, cards enabled.
    std::array<float, 4> CardStyle{};
};
static_assert(offsetof(GrassEnvironmentRecord, Toon) == 1424U);
static_assert(offsetof(GrassEnvironmentRecord, PreviousPositionToClip) == 1584U);
static_assert(offsetof(GrassEnvironmentRecord, CardDistances) == 1680U);
static_assert(sizeof(GrassEnvironmentRecord) == 1712U);

// Push constants of the grass draws (GrassState in the shaders).
struct GrassPushConstants {
    std::array<float, 16> PositionToClip{};
    std::array<float, 4> JitterNdc{};
    std::array<float, 4> DepthState{}; // scale, offset, w-buffer, valid
    // native projection, segments (0 = distant tufts) with the bits below,
    // planes, fog-record index
    std::array<uint32_t, 4> Flags{};
};
static_assert(sizeof(GrassPushConstants) == 112U);

// Flags[1] of a blade draw: grouped midrange children, and the visibility
// split of a raster-depth batch drawn twice. The whole pass draws only the
// blades that are wholly visible (no LOD stipple, no tufts) with the program
// without discard, so early depth testing rejects hidden fragments; the
// fading pass draws the rest with the stipple. Neither bit: every blade.
inline constexpr uint32_t kGrassGroupedFlag = 0x80000000U;
inline constexpr uint32_t kGrassWholePassFlag = 0x40000000U;
inline constexpr uint32_t kGrassFadingPassFlag = 0x20000000U;
inline constexpr uint32_t kGrassSegmentModeMask = 0x1FFFFFFFU;

// The blades' PICA depth equals the rasterized depth (bit for bit): no PICA
// mapping, or Z-buffering with scale -1 / offset 0 on the negated window
// depth (scale 1 on the plain one). Such batches draw with the fragment
// program that leaves depth to fixed function and keeps early depth testing.
[[nodiscard]] inline bool GrassDepthIsRasterDepth(const GrassPushConstants& push) noexcept {
    const auto& depth = push.DepthState;
    if (depth[3] <= 0.5F) return true;
    return depth[2] <= 0.5F && depth[1] == 0.0F && depth[0] == (push.Flags[0] != 0U ? -1.0F : 1.0F);
}

} // namespace Fast::Oot3d
