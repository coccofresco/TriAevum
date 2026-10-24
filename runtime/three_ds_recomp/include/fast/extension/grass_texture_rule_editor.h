#pragma once

#include "fast/extension/grass_types.h"

namespace Fast::Extension {

// Draws only the controls that describe how one source texture selects
// surfaces. Generated-grass controls are intentionally global.
// Returns true only when the persistent settings model changed.
bool DrawGrassTextureRuleControls(GrassPlacementRule& rule, bool surfaceControls = true, float previewStrength = 1.0F);

} // namespace Fast::Extension
