#pragma once

#include "fast/extension/grass_world_placement_cache.h"
#include "fast/renderer3ds/virtual_shadow_map.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Extension {

inline constexpr uint32_t kGrassShadowRefinementPointBudget = 4096;
inline constexpr uint32_t kGrassShadowRefinementPatchesPerActor = 128;

// std430, shared by the grid producer and Grass vertex receiver. The lattice
// drops the dominant normal axis, remaining world-anchored on slopes/walls.
struct GrassShadowRefinementPatch {
    std::array<float, 4> A{};          // Native triangle vertex; w = lattice spacing.
    std::array<float, 4> B{};          // Native triangle vertex; w = boundary fade width.
    std::array<float, 4> C{};          // Native triangle vertex; w = focus slot (CPU only).
    std::array<float, 4> Normal{};     // xyz = native plane; w = dropped axis bits.
    std::array<float, 4> Footprint{};  // Projected actor min(U,V), max(U,V).
    std::array<float, 4> Grid{};       // Origin(U,V), inverse spacing, selection distance.
    std::array<uint32_t, 4> Address{}; // First local sample, columns, rows, surface.
};
static_assert(sizeof(GrassShadowRefinementPatch) == 112);

struct GrassShadowGridVisibility {
    float Visibility = 1.0F;
    uint32_t Patches = 0; // Two uint16 patch indices, plus one; zero = coarse only.
};
static_assert(sizeof(GrassShadowGridVisibility) == 8);

struct GrassShadowRefinement {
    std::vector<GrassShadowRefinementPatch> Patches;
    std::vector<std::array<uint32_t, 2>> SurfacePatches;
    uint32_t PointCount = 0;
    float CellSize = 4.0F;
};

// Focus bounds come from the captured native caster pose, not collision
// cylinders or root density. Reuses output storage; never visits Grass roots.
void BuildGrassShadowRefinement(std::span<const GrassShadowSurface> surfaces,
                                const std::array<Renderer3ds::ShadowBounds, 2>& focus,
                                const Renderer3ds::ShadowVector& directionTowardLight, GrassShadowRefinement& output);

} // namespace Fast::Extension
