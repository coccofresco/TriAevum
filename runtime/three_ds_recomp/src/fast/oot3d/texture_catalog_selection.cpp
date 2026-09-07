#include "fast/oot3d/texture_catalog_selection.h"

#include <algorithm>
#include <cstddef>

namespace Fast::Oot3d {
namespace {

bool ContainsHash(std::span<const uint64_t> hashes, uint64_t hash) {
    return std::find(hashes.begin(), hashes.end(), hash) !=
        hashes.end();
}

} // namespace

TextureCatalogIdentity TextureCatalogIdentityOf(
    const TextureCatalogEntry& entry) noexcept {
    return {
        entry.ContentHash,
        entry.Width,
        entry.Height,
        entry.NativeFormat,
    };
}

bool IsTextureCatalogEntrySelected(
    const TextureCatalogSelectionState& state,
    const TextureCatalogEntry& entry) noexcept {
    return state.Selected.has_value() &&
        *state.Selected == TextureCatalogIdentityOf(entry);
}

void SelectTextureCatalogEntry(
    TextureCatalogSelectionState& state,
    const TextureCatalogEntry& entry) noexcept {
    state.Selected = TextureCatalogIdentityOf(entry);
}

std::vector<TextureCatalogEntry> SortTextureCatalogEntries(
    std::vector<TextureCatalogEntry> entries,
    TextureCatalogSortMode sortMode,
    std::span<const uint64_t> assignedHashes) {
    std::stable_sort(
        entries.begin(), entries.end(),
        [&](const auto& left, const auto& right) {
            if (sortMode ==
                TextureCatalogSortMode::AssignedFirst) {
                const bool leftAssigned =
                    ContainsHash(assignedHashes,
                                 left.ContentHash);
                const bool rightAssigned =
                    ContainsHash(assignedHashes,
                                 right.ContentHash);
                if (leftAssigned != rightAssigned) {
                    return leftAssigned;
                }
            } else if (
                sortMode == TextureCatalogSortMode::Largest) {
                const uint64_t leftArea =
                    static_cast<uint64_t>(left.Width) *
                    left.Height;
                const uint64_t rightArea =
                    static_cast<uint64_t>(right.Width) *
                    right.Height;
                if (leftArea != rightArea) {
                    return leftArea > rightArea;
                }
            } else if (
                sortMode == TextureCatalogSortMode::Hash) {
                if (left.ContentHash != right.ContentHash) {
                    return left.ContentHash <
                        right.ContentHash;
                }
            }
            if (left.Observations != right.Observations) {
                return left.Observations >
                    right.Observations;
            }
            if (left.ContentHash != right.ContentHash) {
                return left.ContentHash < right.ContentHash;
            }
            if (left.Width != right.Width) {
                return left.Width < right.Width;
            }
            if (left.Height != right.Height) {
                return left.Height < right.Height;
            }
            return left.NativeFormat < right.NativeFormat;
        });
    return entries;
}

std::optional<TextureCatalogEntry>
ResolveTextureCatalogSelection(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogEntry> entries,
    std::span<const uint64_t> preferredHashes) {
    if (entries.empty()) {
        state.Selected.reset();
        return std::nullopt;
    }
    const auto findHash = [&](uint64_t hash) {
        return std::find_if(
            entries.begin(), entries.end(),
            [&](const auto& entry) {
                return entry.ContentHash == hash;
            });
    };
    auto selected = state.Selected.has_value()
        ? std::find_if(
              entries.begin(), entries.end(),
              [&](const auto& entry) {
                  return TextureCatalogIdentityOf(entry) ==
                      *state.Selected;
              })
        : entries.end();
    if (selected == entries.end()) {
        for (const uint64_t preferred : preferredHashes) {
            selected = findHash(preferred);
            if (selected != entries.end()) {
                break;
            }
        }
    }
    if (selected == entries.end()) {
        selected = entries.begin();
    }
    SelectTextureCatalogEntry(state, *selected);
    return *selected;
}

std::optional<TextureCatalogEntry>
MoveTextureCatalogSelection(
    TextureCatalogSelectionState& state,
    std::span<const TextureCatalogEntry> entries,
    int offset) {
    const auto current =
        ResolveTextureCatalogSelection(state, entries);
    if (!current.has_value() || entries.empty()) {
        return std::nullopt;
    }
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [&](const auto& entry) {
            return IsTextureCatalogEntrySelected(
                state, entry);
        });
    const ptrdiff_t currentIndex =
        found == entries.end()
        ? 0
        : std::distance(entries.begin(), found);
    const ptrdiff_t count =
        static_cast<ptrdiff_t>(entries.size());
    const ptrdiff_t normalizedOffset =
        static_cast<ptrdiff_t>(offset) % count;
    const ptrdiff_t nextIndex =
        (currentIndex + normalizedOffset + count) %
        count;
    SelectTextureCatalogEntry(
        state, entries[static_cast<size_t>(nextIndex)]);
    return entries[static_cast<size_t>(nextIndex)];
}

} // namespace Fast::Oot3d
