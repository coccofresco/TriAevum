#pragma once

#include "fast/extension/graphics_settings.h"
#include "fast/extension/texture_catalog_selection.h"

#include <string>

namespace Fast::Extension {

struct GrassSettingsPanelState {
    TextureCatalogSelectionState TextureSelection;
    uint32_t SelectedSourceRuleId = 0;
    GrassTextureSelector SelectedMask;
    TextureCatalogSelectionState MaskTextureSelection;
    bool HighlightMask = false;
    std::string Status;
};

// Draws the complete interactive-grass editor. Texture assignment, placement,
// appearance and runtime controls intentionally share this one feature-owned
// surface rather than leaking into general renderer settings.
bool DrawGrassSettingsPanel(
    InteractiveGrassSettings& grass,
    InteractiveGrassSettings& savedPreset,
    GrassSettingsPanelState& state);

} // namespace Fast::Extension
