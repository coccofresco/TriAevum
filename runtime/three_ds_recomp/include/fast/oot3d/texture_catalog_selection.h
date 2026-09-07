#pragma once

#include "fast/oot3d/texture_catalog_runtime.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Fast::Oot3d {

enum class TextureCatalogSortMode : uint8_t {
    MostObserved,
    Largest,
    Hash,
    AssignedFirst,
};

struct TextureCatalogIdentity {
    uint64_t ContentHash = 0U;
    uint16_t Width = 0U;
    uint16_t Height = 0U;
    uint8_t NativeFormat = 0U;

    bool operator==(const TextureCatalogIdentity&) const = default;
};

struct TextureCatalogSelectionState {
    TextureCatalogSortMode SortMode =
        TextureCatalogSortMode::MostObserved;
    std::optional<TextureCatalogIdentity> Selected;
    // Metadata only: an open menu must not reorder under the pointer as the
    // renderer observes textures. Refreshed on every new popup opening.
    std::vector<TextureCatalogEntry> PopupEntries;
};

[[nodiscard]] TextureCatalogIdentity TextureCatalogIdentityOf(
    const TextureCatalogEntry& entry) noexcept;
[[nodiscard]] bool IsTextureCatalogEntrySelected(
    const TextureCatalogSelectionState& state,
    const TextureCatalogEntry& entry) noexcept;
void SelectTextureCatalogEntry(
    TextureCatalogSelectionState& state,
    const TextureCatalogEntry& entry) noexcept;

[[nodiscard]] std::vector<TextureCatalogEntry>
SortTextureCatalogEntries(
    std::vector<TextureCatalogEntry> entries,
    TextureCatalogSortMode sortMode,
    std::span<const uint64_t> assignedHashes = {});

[[nodiscard]] std::optional<TextureCatalogEntry>
ResolveTextureCatalogSelection(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogEntry> entries,
    std::span<const uint64_t> preferredHashes = {});

[[nodiscard]] std::optional<TextureCatalogEntry>
MoveTextureCatalogSelection(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogEntry> entries,
    int offset);

} // namespace Fast::Oot3d
