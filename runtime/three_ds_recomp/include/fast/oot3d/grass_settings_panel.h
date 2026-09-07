#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/texture_catalog_selection.h"

#include <string>

namespace Fast::Oot3d {

struct GrassSettingsPanelState {
    TextureCatalogSelectionState TextureSelection;
    uint32_t SelectedSourceRuleId = 0;
    std::string Status;
};

// Draws the complete interactive-grass editor. Texture assignment, placement,
// appearance and runtime controls intentionally share this one feature-owned
// surface rather than leaking into general renderer settings.
bool DrawGrassSettingsPanel(
    InteractiveGrassSettings& grass,
    InteractiveGrassSettings& savedPreset,
    GrassSettingsPanelState& state);

} // namespace Fast::Oot3d
