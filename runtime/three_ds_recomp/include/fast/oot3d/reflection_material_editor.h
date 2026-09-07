#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/texture_catalog_runtime.h"

namespace Fast::Oot3d {

// Draws profile parameters for the reflection rule assigned to the exact
// catalog identity.
// Catalog browsing and assignment are owned by the shared texture viewer.
bool DrawReflectionMaterialControls(
    const TextureCatalogEntry& texture,
    EffectsSettings& effects);

} // namespace Fast::Oot3d
