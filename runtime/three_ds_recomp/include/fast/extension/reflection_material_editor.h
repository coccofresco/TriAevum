#pragma once

#include "fast/extension/graphics_settings.h"
#include "fast/extension/texture_catalog_runtime.h"

namespace Fast::Extension {

// Draws profile parameters for the reflection rule assigned to the exact
// catalog identity.
// Catalog browsing and assignment are owned by the shared texture viewer.
bool DrawReflectionMaterialControls(
    const TextureCatalogEntry& texture,
    EffectsSettings& effects);

} // namespace Fast::Extension
