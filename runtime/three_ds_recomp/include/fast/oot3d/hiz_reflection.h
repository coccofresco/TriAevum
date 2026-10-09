#pragma once

#include "fast/renderer3ds/pica_scene_water.h"
#include "fast/renderer3ds/pica_water_map.h"

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

[[nodiscard]] std::string BuildHiZReflectionComputeShader();
[[nodiscard]] std::string BuildHiZReflectionBilateralFilterShader();

// Gameplay water for the trace. The title's transparent water writes no
// depth, so the trace finds the water surface itself: per pixel, the nearest
// listed body's horizontal surface the view ray meets inside its footprint,
// in front of the scene depth. Waves and Fresnel as the surface shader.
inline constexpr uint32_t kHiZWaterPlanes = 8U;

struct alignas(16) HiZWaterUniforms {
    // View to world, rows (xyz linear, w translation: the camera).
    std::array<std::array<float, 4>, 3> ViewToWorld{};
    // Bodies' footprints (min x, min z, max x, max z) and heights.
    std::array<std::array<float, 4>, kHiZWaterPlanes> Footprints{};
    std::array<std::array<float, 4>, kHiZWaterPlanes / 4U> Heights{};
    // Body count, wave time (seconds), Fresnel F0, water only (1: nothing
    // else reflects).
    std::array<float, 4> Parameters{};
    // The water as a medium, composed by the filter over the scene: light
    // absorption per world unit of path (green; red and blue scale it),
    // refraction (view units of displacement per unit of slope and of
    // water depth, capped), shore foam width (world units of depth) and
    // the composed water's opacity over the scene (0: reflections only).
    std::array<float, 4> Medium{};
    // Movers' ripple rings: centre x, z, birth (wave time), strength.
    std::array<std::array<float, 4>, Renderer3ds::kPicaWaterRings> Rings{};
    // The drawn water's map (PicaWaterMap, binding 9): origin x, z, world
    // units per texel and texels per side (0: none); its levels (surface
    // heights, ascending) and their count. A level is water where the map
    // holds its height.
    std::array<float, 4> MapInfo{};
    std::array<std::array<float, 4>, Renderer3ds::kPicaWaterMapLevels / 4U> MapLevels{};
    std::array<float, 4> MapCounts{};
};
static_assert(sizeof(HiZWaterUniforms) == 592U);

// The bodies below the camera (seen from above), nearest first; without a
// valid view, none. waterOnly is kept either way.
[[nodiscard]] HiZWaterUniforms BuildHiZWaterUniforms(const std::array<float, 16>* worldToView,
                                                     const Renderer3ds::PicaSceneWaterBodies* bodies,
                                                     float seconds, bool waterOnly,
                                                     const std::array<Renderer3ds::PicaWaterRing,
                                                                      Renderer3ds::kPicaWaterRings>* rings = nullptr,
                                                     const Renderer3ds::PicaWaterMap* map = nullptr);

} // namespace Fast::Oot3d
