#pragma once

#include "fast/extension/graphics_settings.h"

#include <array>

namespace Fast::Extension {

// Returns world-XZ bend as a fraction of blade height.
[[nodiscard]] std::array<float, 2> EvaluateGrassWind(
    const InteractiveGrassSettings& settings,
    const std::array<float, 3>& worldPosition, float anchorPhase,
    double visualSeconds);

} // namespace Fast::Extension
