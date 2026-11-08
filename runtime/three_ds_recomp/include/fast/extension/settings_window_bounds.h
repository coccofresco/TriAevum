#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Fast::Extension {
// Size constraints alone do not keep a window's footer on screen: a 704px
// window starting at y=60 still overflows a 720px viewport. Preserve valid
// placement, but bring the complete constrained window into the work area.
inline std::array<float, 2> FitSettingsWindowPosition(
    std::array<float, 2> position, const std::array<float, 2>& size,
    const std::array<float, 2>& workPosition, const std::array<float, 2>& workSize) {
    for (unsigned axis = 0; axis < 2; ++axis) {
        const float minimum = workPosition[axis] + 8.0F;
        const float maximum = std::max(minimum, workPosition[axis] + workSize[axis] - size[axis] - 8.0F);
        position[axis] = std::clamp(std::isfinite(position[axis]) ? position[axis] : minimum, minimum, maximum);
    }
    return position;
}
} // namespace Fast::Extension
