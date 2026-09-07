#pragma once

#include "fast/oot3d/grass_types.h"

namespace Fast::Oot3d {

// Draws only the controls that describe how one source texture selects
// surfaces. Generated-grass controls are intentionally global.
// Returns true only when the persistent settings model changed.
bool DrawGrassTextureRuleControls(
    GrassPlacementRule& rule);

} // namespace Fast::Oot3d
