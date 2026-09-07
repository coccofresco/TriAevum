#pragma once

#include "fast/oot3d/graphics_settings.h"

#include <array>

namespace Fast::Oot3d {

// Returns world-XZ bend as a fraction of blade height.
[[nodiscard]] std::array<float, 2> EvaluateGrassWind(
    const InteractiveGrassSettings& settings,
    const std::array<float, 3>& worldPosition, float anchorPhase,
    double visualSeconds);

} // namespace Fast::Oot3d
