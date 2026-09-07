#include "oot3d_n64_gameplay_hud.h"

#include "oot3d_native_hud_atlas_regions.h"

#include "oot3d_ui/ui_pause_atlas_regions.h"
#include "oot3d_ui/ui_value_domains.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace oot3d::ui {
namespace {

constexpr UiColor kWhite{1.0F, 1.0F, 1.0F, 1.0F};
constexpr float kDisabledButtonAlpha = 70.0F / 255.0F;

bool IsHidden(HudVisibilityMode mode) noexcept {
    switch (mode) {
    case HudVisibilityMode::HUD_VISIBILITY_NOTHING:
    case HudVisibilityMode::HUD_VISIBILITY_NOTHING_ALT:
    case HudVisibilityMode::HUD_VISIBILITY_NOTHING_INSTANT:
        return true;
    default:
        return false;
    }
}

UiRect NormalizeAtlasRect(const Oot3dHudAssetBinding& binding) noexcept {
    const float width = static_cast<float>(binding.atlas_width);
    const float height = static_cast<float>(binding.atlas_height);
    return {binding.atlas_pixels.x / width,
            binding.atlas_pixels.y / height,
            binding.atlas_pixels.width / width,
            binding.atlas_pixels.height / height};
}

bool AppendBoundPrimitive(const Oot3dHudAssetCatalog& assets,
                          Oot3dHudAssetKey key, UiPrimitiveRole role,
                          UiRect destination, UiColor color,
                          std::uint32_t layer,
                          std::uint32_t source_quad,
                          std::vector<UiPrimitive>& output,
                          N64GameplayHudBuildStats& stats,
                          float horizontal_uv_fraction = 1.0F) {
    const Oot3dHudAssetBinding* binding = assets.Find(key);
    if (binding == nullptr) {
        ++stats.missing_asset_bindings;
        return false;
    }

    UiPrimitive primitive;
    primitive.subsystem = UiSubsystem::GameplayHud;
    primitive.role = role;
    primitive.source_quad = source_quad;
    primitive.texture = binding->texture;
    primitive.destination = destination;
    primitive.uv = NormalizeAtlasRect(*binding);
    const float fraction = std::clamp(horizontal_uv_fraction, 0.0F, 1.0F);
    primitive.uv.width *= fraction;
    primitive.destination.width *= fraction;
    primitive.color = color;
    primitive.layer = layer;
    output.push_back(std::move(primitive));
    ++stats.primitives_emitted;
    return true;
}

void AppendDecimal(std::uint32_t value, std::uint8_t digit_count,
                   bool suppress_leading_zeroes, std::uint8_t style,
                   UiPrimitiveRole role, UiRect destination, UiColor color,
                   std::uint32_t source_quad_base,
                   const Oot3dHudAssetCatalog& assets,
                   std::vector<UiPrimitive>& output,
                   N64GameplayHudBuildStats& stats) {
    if (digit_count == 0U) {
        return;
    }
    std::array<std::uint8_t, 5> digits{};
    const std::size_t count = std::min<std::size_t>(digit_count, digits.size());
    std::uint32_t divisor = 1U;
    for (std::size_t index = 1; index < count; ++index) {
        divisor *= 10U;
    }
    const std::uint32_t maximum = divisor * 10U - 1U;
    value = std::min(value, maximum);

    for (std::size_t index = 0; index < count; ++index) {
        digits[index] = static_cast<std::uint8_t>((value / divisor) % 10U);
        divisor = std::max<std::uint32_t>(1U, divisor / 10U);
    }

    std::size_t first = 0U;
    if (suppress_leading_zeroes) {
        while (first + 1U < count && digits[first] == 0U) {
            ++first;
        }
    }
    const float digit_width = destination.width / static_cast<float>(count);
    for (std::size_t index = first; index < count; ++index) {
        UiRect digit_destination = destination;
        digit_destination.x += static_cast<float>(index) * digit_width;
        digit_destination.width = digit_width;
        AppendBoundPrimitive(
            assets,
            {Oot3dHudAssetKind::CounterDigit,
             Oot3dHudDigitVariant(style, 0U, digits[index])},
            role, digit_destination, color, 30U,
            source_quad_base + static_cast<std::uint32_t>(index), output,
            stats);
    }
}

} // namespace

bool IsOot3dHudTextureIdentity(
    const UiTextureIdentity& texture) noexcept {
    return std::string_view(texture.semantic_name).starts_with("oot3d/");
}

bool Oot3dHudAssetCatalog::Add(Oot3dHudAssetBinding binding,
                               std::string* error) {
    if (!IsOot3dHudTextureIdentity(binding.texture)) {
        if (error != nullptr) {
            *error = "HUD asset is not owned by the oot3d namespace";
        }
        return false;
    }
    if (binding.atlas_width == 0U || binding.atlas_height == 0U ||
        binding.atlas_pixels.width <= 0.0F ||
        binding.atlas_pixels.height <= 0.0F || binding.atlas_pixels.x < 0.0F ||
        binding.atlas_pixels.y < 0.0F ||
        binding.atlas_pixels.x + binding.atlas_pixels.width >
            static_cast<float>(binding.atlas_width) ||
        binding.atlas_pixels.y + binding.atlas_pixels.height >
            static_cast<float>(binding.atlas_height)) {
        if (error != nullptr) {
            *error = "HUD asset region is outside its OoT3D atlas";
        }
        return false;
    }
    if (Find(binding.key) != nullptr) {
        if (error != nullptr) {
            *error = "duplicate OoT3D HUD asset key";
        }
        return false;
    }
    bindings_.push_back(std::move(binding));
    return true;
}

const Oot3dHudAssetBinding* Oot3dHudAssetCatalog::Find(
    Oot3dHudAssetKey key) const noexcept {
    const auto found = std::find_if(
        bindings_.begin(), bindings_.end(),
        [key](const Oot3dHudAssetBinding& binding) {
            return binding.key == key;
        });
    return found == bindings_.end() ? nullptr : &*found;
}

std::size_t Oot3dHudAssetCatalog::Size() const noexcept {
    return bindings_.size();
}

Oot3dHudAssetCatalog BuildVerifiedOot3dHudAssetCatalog(
    const Oot3dHudTextureSources& sources) {
    Oot3dHudAssetCatalog result;
    if (IsOot3dHudTextureIdentity(sources.pause_top_page)) {
        const auto add_pause_top_region =
            [&result, &sources](Oot3dHudAssetKey key,
                                Oot3dNativeHudAtlasRect region) {
                (void)result.Add(
                    {key, sources.pause_top_page,
                     {static_cast<float>(region.x),
                      static_cast<float>(region.y),
                      static_cast<float>(region.width),
                      static_cast<float>(region.height)},
                     kOot3dPauseTopPageAtlasWidth,
                     kOot3dPauseTopPageAtlasHeight});
            };
        add_pause_top_region({Oot3dHudAssetKind::RupeeIcon, 0U},
                             kOot3dRupeeAtlasRect);
        add_pause_top_region({Oot3dHudAssetKind::SmallKeyIcon, 0U},
                             kOot3dSmallKeyAtlasRect);
        for (std::uint8_t fraction = 0U; fraction <= 16U; ++fraction) {
            add_pause_top_region(
                {Oot3dHudAssetKind::Heart,
                 Oot3dHudHeartVariant(fraction, false)},
                Oot3dHeartAtlasRect(fraction, false));
            add_pause_top_region(
                {Oot3dHudAssetKind::Heart,
                 Oot3dHudHeartVariant(fraction, true)},
                Oot3dHeartAtlasRect(fraction, true));
        }
    }
    if (IsOot3dHudTextureIdentity(sources.item_icons)) {
        for (const auto& region : Oot3dItemIconAtlasRegions()) {
            if (!region.IsDrawable()) {
                continue;
            }
            (void)result.Add(
                {{Oot3dHudAssetKind::AssignedItem,
                  static_cast<std::uint16_t>(UiValueRaw(region.item_id))},
                 sources.item_icons,
                 {static_cast<float>(region.rect.x),
                  static_cast<float>(region.rect.y),
                  static_cast<float>(region.rect.width),
                  static_cast<float>(region.rect.height)},
                 kOot3dItemIconAtlasWidth, kOot3dItemIconAtlasHeight});
        }
    }
    if (IsOot3dHudTextureIdentity(sources.number_glyphs)) {
        for (const auto& region : Oot3dNumberGlyphAtlasRegions()) {
            if (!region.IsDrawable()) {
                continue;
            }
            (void)result.Add(
                {{Oot3dHudAssetKind::CounterDigit,
                  Oot3dHudDigitVariant(region.counter_style,
                                       region.selection_variant,
                                       region.digit)},
                 sources.number_glyphs,
                 {static_cast<float>(region.rect.x),
                  static_cast<float>(region.rect.y),
                  static_cast<float>(region.rect.width),
                  static_cast<float>(region.rect.height)},
                 kOot3dNumberGlyphAtlasWidth,
                 kOot3dNumberGlyphAtlasHeight});
        }
    }
    return result;
}

N64GameplayHudLayout BuildCanonicalN64GameplayHudLayout() noexcept {
    N64GameplayHudLayout result;
    const float right_anchor_offset = result.canvas_width - 320.0F;

    // Original 320x240 positions come from z64interface.h/z_construct.c. The
    // right-anchor offset is the widescreen equivalent of Ship's OTR helper.
    result.first_heart = {0.0F, 0.0F, 11.0F, 11.0F};
    result.magic_frame = {18.0F, 34.0F, 64.0F, 16.0F};
    result.magic_fill = {26.0F, 37.0F, 48.0F, 7.0F};
    result.rupee_icon = {26.0F, 206.0F, 16.0F, 16.0F};
    result.rupee_digits = {42.0F, 206.0F, 24.0F, 16.0F};
    result.small_key_icon = {26.0F, 190.0F, 16.0F, 16.0F};
    result.small_key_digits = {42.0F, 190.0F, 16.0F, 16.0F};

    constexpr std::array<float, kOot3dButtonItemCount> source_x = {
        160.0F, 227.0F, 249.0F, 271.0F, 254.0F};
    constexpr std::array<float, kOot3dButtonItemCount> source_y = {
        17.0F, 18.0F, 34.0F, 18.0F, 16.0F};
    constexpr std::array<float, kOot3dButtonItemCount> face_size = {
        30.0F, 27.0F, 27.0F, 27.0F, 16.0F};
    constexpr std::array<float, kOot3dButtonItemCount> icon_size = {
        30.0F, 24.0F, 24.0F, 24.0F, 16.0F};
    for (std::size_t index = 0; index < result.button_faces.size(); ++index) {
        const float x = source_x[index] + right_anchor_offset;
        const float inset = (face_size[index] - icon_size[index]) * 0.5F;
        result.button_faces[index] =
            {x, source_y[index], face_size[index], face_size[index]};
        result.item_icons[index] =
            {x + inset, source_y[index] + inset, icon_size[index],
             icon_size[index]};
        result.ammo_digits[index] =
            {x + 1.0F, source_y[index] + 17.0F, 16.0F, 8.0F};
    }
    return result;
}

N64GameplayHudBuildStats AppendN64GameplayHudPresentation(
    const UiHudContentSnapshot& content,
    const Oot3dHudAssetCatalog& assets,
    const N64GameplayHudLayout& layout,
    std::vector<UiPrimitive>& output) {
    N64GameplayHudBuildStats stats;
    if (content.visibility.current.IsKnown() &&
        IsHidden(content.visibility.current.value)) {
        return stats;
    }

    if (content.health.capacity_hearts.IsKnown() &&
        content.health.current_units.IsKnown()) {
        const std::uint16_t heart_count = content.health.capacity_hearts.value;
        const bool double_defense =
            content.health.double_defense_acquired.IsKnown() &&
            content.health.double_defense_acquired.value;
        for (std::uint16_t index = 0; index < heart_count; ++index) {
            const std::int32_t remaining =
                static_cast<std::int32_t>(content.health.current_units.value) -
                static_cast<std::int32_t>(index) *
                    kOot3dHealthUnitsPerHeart;
            const auto fraction = static_cast<std::uint8_t>(
                std::clamp(remaining, 0,
                           static_cast<std::int32_t>(
                               kOot3dHealthUnitsPerHeart)));
            UiRect destination = layout.first_heart;
            destination.x += static_cast<float>(index % layout.hearts_per_row) *
                             layout.heart_step_x;
            destination.y += static_cast<float>(index / layout.hearts_per_row) *
                             layout.heart_step_y;
            AppendBoundPrimitive(
                assets,
                {Oot3dHudAssetKind::Heart,
                 Oot3dHudHeartVariant(fraction, double_defense)},
                UiPrimitiveRole::Heart, destination, kWhite, 10U, index,
                output, stats);
        }
    } else {
        ++stats.unknown_content_values;
    }

    if (content.magic.acquired.IsKnown() && content.magic.acquired.value &&
        content.magic.current.IsKnown() && content.magic.capacity.IsKnown() &&
        content.magic.capacity.value > 0) {
        const bool double_magic = content.magic.double_magic_acquired.IsKnown() &&
                                  content.magic.double_magic_acquired.value;
        AppendBoundPrimitive(
            assets,
            {Oot3dHudAssetKind::MagicFrame,
             static_cast<std::uint16_t>(double_magic ? 1U : 0U)},
            UiPrimitiveRole::MagicFrame, layout.magic_frame, kWhite, 10U, 0U,
            output, stats);
        const float fill = std::clamp(
            static_cast<float>(content.magic.current.value) /
                static_cast<float>(content.magic.capacity.value),
            0.0F, 1.0F);
        AppendBoundPrimitive(
            assets, {Oot3dHudAssetKind::MagicFill, 0U},
            UiPrimitiveRole::MagicFill, layout.magic_fill, kWhite, 11U, 0U,
            output, stats, fill);
    }

    if (content.counters.rupees.IsKnown()) {
        AppendBoundPrimitive(assets, {Oot3dHudAssetKind::RupeeIcon, 0U},
                             UiPrimitiveRole::RupeeIcon, layout.rupee_icon,
                             kWhite, 20U, 0U, output, stats);
        AppendDecimal(
            static_cast<std::uint32_t>(
                std::max<std::int16_t>(0, content.counters.rupees.value)),
            3U, false, layout.rupee_digit_style,
            UiPrimitiveRole::CounterDigit, layout.rupee_digits, kWhite, 0U,
            assets, output, stats);
    }

    if (content.counters.current_dungeon_keys.IsKnown() &&
        content.counters.current_dungeon_keys.value >= 0) {
        AppendBoundPrimitive(assets, {Oot3dHudAssetKind::SmallKeyIcon, 0U},
                             UiPrimitiveRole::SmallKeyIcon,
                             layout.small_key_icon, kWhite, 20U, 0U, output,
                             stats);
        AppendDecimal(
            static_cast<std::uint32_t>(
                content.counters.current_dungeon_keys.value),
            2U, true, layout.ammo_digit_style,
            UiPrimitiveRole::CounterDigit, layout.small_key_digits, kWhite,
            10U, assets, output, stats);
    }

    for (std::size_t index = 0; index < content.buttons.size(); ++index) {
        const UiHudButtonContent& button = content.buttons[index];
        UiColor color = kWhite;
        if (button.status.IsKnown() &&
            button.status.value == ButtonStatus::BTN_DISABLED) {
            color.alpha = kDisabledButtonAlpha;
        } else if (!button.status.IsKnown()) {
            ++stats.unknown_content_values;
        }
        AppendBoundPrimitive(
            assets,
            {Oot3dHudAssetKind::ButtonFace,
             static_cast<std::uint16_t>(index)},
            UiPrimitiveRole::ActionButton, layout.button_faces[index], color,
            20U, static_cast<std::uint32_t>(index), output, stats);

        if (!button.item_id.IsKnown()) {
            ++stats.unknown_content_values;
            continue;
        }
        if (button.item_id.value == ItemId::ITEM_NONE ||
            button.item_id.value == ItemId::ITEM_NONE_FE) {
            continue;
        }
        const bool item_emitted = AppendBoundPrimitive(
            assets,
            {Oot3dHudAssetKind::AssignedItem,
             static_cast<std::uint16_t>(UiValueRaw(button.item_id.value))},
            UiPrimitiveRole::AssignedItem, layout.item_icons[index], color,
            21U, static_cast<std::uint32_t>(index), output, stats);
        if (!item_emitted || !button.ammo.IsKnown() ||
            button.ammo.value < 0) {
            continue;
        }
        AppendDecimal(static_cast<std::uint32_t>(button.ammo.value), 2U,
                      true, layout.ammo_digit_style,
                      UiPrimitiveRole::AmmoCounter,
                      layout.ammo_digits[index], color,
                      100U + static_cast<std::uint32_t>(index) * 10U,
                      assets, output, stats);
    }
    return stats;
}

} // namespace oot3d::ui
