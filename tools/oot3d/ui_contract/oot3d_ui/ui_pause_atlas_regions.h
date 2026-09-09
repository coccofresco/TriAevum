#pragma once

#include "oot3d_ui/ui_localized_menu_resources.h"
#include "oot3d_ui/ui_value_domains.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

struct UiAtlasRect {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    constexpr bool FitsWithin(std::uint16_t atlas_width,
                              std::uint16_t atlas_height) const noexcept {
        return static_cast<std::uint32_t>(x) + width <= atlas_width &&
               static_cast<std::uint32_t>(y) + height <= atlas_height;
    }
};

// These states preserve the result of the original OoT3D coordinate rules.
// A projected rectangle is not automatically a promise that native code uses
// that value as a drawable atlas entry.
enum class UiPauseAtlasRegionStatus : std::uint8_t {
    DrawableInBounds,
    ProjectionOutsideAtlas,
    EmptyItem,
    NativeUpdateBypassed,
    InvalidNativeSelection,
    Count,
};

struct UiItemIconAtlasRegionDescriptor {
    ItemId item_id = ItemId::ITEM_NONE;
    const char* item_name = nullptr;
    UiPauseSharedTextureSlot texture_slot = UiPauseSharedTextureSlot::ItemIcons;
    UiAtlasRect rect{};
    UiPauseAtlasRegionStatus status =
        UiPauseAtlasRegionStatus::ProjectionOutsideAtlas;

    constexpr bool IsDrawable() const noexcept {
        return status == UiPauseAtlasRegionStatus::DrawableInBounds;
    }

    constexpr bool HasNativeProjection() const noexcept {
        return status == UiPauseAtlasRegionStatus::DrawableInBounds ||
               status == UiPauseAtlasRegionStatus::ProjectionOutsideAtlas ||
               status == UiPauseAtlasRegionStatus::EmptyItem;
    }
};

struct UiNumberGlyphProfileDescriptor {
    std::uint8_t counter_style = 0;
    std::uint8_t digit_count = 0;
    std::uint16_t base_y = 0;
    std::uint16_t glyph_height = 0;
    std::uint8_t native_constructor_call_sites = 0;
    std::uint8_t valid_selection_variants = 0;
    bool decimal_update_enabled = false;
};

struct UiNumberGlyphAtlasRegionDescriptor {
    std::uint8_t counter_style = 0;
    std::uint8_t selection_variant = 0;
    std::uint8_t digit = 0;
    UiPauseSharedTextureSlot texture_slot =
        UiPauseSharedTextureSlot::NumberGlyphs;
    UiAtlasRect rect{};
    UiPauseAtlasRegionStatus status =
        UiPauseAtlasRegionStatus::InvalidNativeSelection;

    constexpr bool IsDrawable() const noexcept {
        return status == UiPauseAtlasRegionStatus::DrawableInBounds;
    }

    constexpr bool HasNativeProjection() const noexcept {
        return status == UiPauseAtlasRegionStatus::DrawableInBounds ||
               status == UiPauseAtlasRegionStatus::ProjectionOutsideAtlas;
    }
};

inline constexpr std::uint16_t kOot3dItemIconAtlasWidth = 512;
inline constexpr std::uint16_t kOot3dItemIconAtlasHeight = 512;
inline constexpr std::uint16_t kOot3dItemIconExtent = 42;
inline constexpr std::uint8_t kOot3dItemIconAtlasColumns = 12;
inline constexpr std::size_t kOot3dItemIconAtlasRegionCount = 159;

inline constexpr std::uint16_t kOot3dNumberGlyphAtlasWidth = 256;
inline constexpr std::uint16_t kOot3dNumberGlyphAtlasHeight = 128;
inline constexpr std::uint16_t kOot3dNumberGlyphWidth = 12;
inline constexpr std::size_t kOot3dNumberGlyphProfileCount = 10;
inline constexpr std::size_t kOot3dNumberGlyphSelectionVariantCount = 2;
inline constexpr std::size_t kOot3dNumberGlyphCount = 10;
inline constexpr std::size_t kOot3dNumberGlyphAtlasRegionCount =
    kOot3dNumberGlyphProfileCount *
    kOot3dNumberGlyphSelectionVariantCount * kOot3dNumberGlyphCount;

const std::array<UiItemIconAtlasRegionDescriptor,
                 kOot3dItemIconAtlasRegionCount>&
Oot3dItemIconAtlasRegions() noexcept;

const UiItemIconAtlasRegionDescriptor* Oot3dItemIconAtlasRegion(
    ItemId item_id) noexcept;

const std::array<UiNumberGlyphProfileDescriptor,
                 kOot3dNumberGlyphProfileCount>&
Oot3dNumberGlyphProfiles() noexcept;

const UiNumberGlyphProfileDescriptor* Oot3dNumberGlyphProfile(
    std::uint8_t counter_style) noexcept;

const std::array<UiNumberGlyphAtlasRegionDescriptor,
                 kOot3dNumberGlyphAtlasRegionCount>&
Oot3dNumberGlyphAtlasRegions() noexcept;

const UiNumberGlyphAtlasRegionDescriptor* Oot3dNumberGlyphAtlasRegion(
    std::uint8_t counter_style, std::uint8_t selection_variant,
    std::uint8_t digit) noexcept;

static_assert(kItemIdValueCount == kOot3dItemIconAtlasRegionCount);
static_assert(kOot3dPauseSharedTextureCount == 16);

} // namespace oot3d::ui
