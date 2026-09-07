#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/grass_types.h"

#include <array>

namespace Fast::Oot3d {

struct GrassCollisionResult {
    std::array<float, 2> Bend{};
    float Weight = 0.0F;
    bool VerticalOverlap = false;
};

// Resolves a blade against the capsule swept between the previous and current
// Link samples. Bend is expressed as a blade-height fraction.
[[nodiscard]] GrassCollisionResult ResolveGrassCollision(
    const std::array<float, 3>& bladeBase, float bladeHeight,
    const GrassInteractor& interactor,
    const InteractiveGrassSettings& settings);

} // namespace Fast::Oot3d
