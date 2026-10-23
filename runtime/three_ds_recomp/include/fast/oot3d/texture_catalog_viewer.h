#pragma once

#include "fast/oot3d/texture_catalog_selection.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace Fast::Oot3d {

struct TextureCatalogTag {
    uint64_t ContentHash = 0U;
    uint16_t Width = 0U;
    uint16_t Height = 0U;
    std::string Label;
};

struct TextureCatalogViewerResult {
    std::optional<TextureCatalogEntry> Selected;
    bool SelectionChanged = false;
};

// Shared texture browser used by every texture-driven renderer feature.
// Feature modules supply badges and assignment controls; this view owns only
// ordering, stable selection and the decoded-pixel preview.
[[nodiscard]] TextureCatalogViewerResult DrawTextureCatalogViewer(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogTag> tags,
    size_t maximumVisibleRows = 12U);

void DrawTexturePixelPreview(std::span<const uint8_t> rgba8, uint32_t width, uint32_t height,
                             float maximumWidth = 260.0F, float maximumHeight = 190.0F);

} // namespace Fast::Oot3d
