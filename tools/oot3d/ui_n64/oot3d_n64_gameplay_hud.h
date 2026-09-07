#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_hud_content.h"
#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oot3d::ui {

enum class Oot3dHudAssetKind : std::uint8_t {
    Heart,
    MagicFrame,
    MagicFill,
    RupeeIcon,
    SmallKeyIcon,
    ButtonFace,
    AssignedItem,
    CounterDigit,
};

struct Oot3dHudAssetKey {
    Oot3dHudAssetKind kind = Oot3dHudAssetKind::Heart;
    std::uint16_t variant = 0;

    constexpr bool operator==(const Oot3dHudAssetKey&) const noexcept =
        default;
};

struct Oot3dHudAssetBinding {
    Oot3dHudAssetKey key;
    UiTextureIdentity texture;
    UiRect atlas_pixels;
    std::uint16_t atlas_width = 0;
    std::uint16_t atlas_height = 0;
};

// Asset regions and texture ownership are an OoT3D concern. The N64 layout
// presenter can only consume bindings accepted by this catalog; it cannot
// resolve an OTR/N64 texture path as a fallback.
class Oot3dHudAssetCatalog {
  public:
    bool Add(Oot3dHudAssetBinding binding, std::string* error = nullptr);
    const Oot3dHudAssetBinding* Find(Oot3dHudAssetKey key) const noexcept;
    std::size_t Size() const noexcept;

  private:
    std::vector<Oot3dHudAssetBinding> bindings_;
};

struct Oot3dHudTextureSources {
    UiTextureIdentity pause_top_page;
    UiTextureIdentity item_icons;
    UiTextureIdentity number_glyphs;
};

// Populates only regions already recovered from original OoT3D atlas tables
// and renderer consumers. Unresolved HUD-atlas regions remain absent.
Oot3dHudAssetCatalog BuildVerifiedOot3dHudAssetCatalog(
    const Oot3dHudTextureSources& sources);

struct N64GameplayHudLayout {
    float canvas_width = 1280.0F / 3.0F;
    float canvas_height = 240.0F;
    std::uint16_t hearts_per_row = 10;
    UiRect first_heart;
    float heart_step_x = 10.0F;
    float heart_step_y = 10.0F;
    UiRect magic_frame;
    UiRect magic_fill;
    UiRect rupee_icon;
    UiRect rupee_digits;
    UiRect small_key_icon;
    UiRect small_key_digits;
    std::array<UiRect, kOot3dButtonItemCount> button_faces{};
    std::array<UiRect, kOot3dButtonItemCount> item_icons{};
    std::array<UiRect, kOot3dButtonItemCount> ammo_digits{};
    std::uint8_t rupee_digit_style = 4;
    std::uint8_t ammo_digit_style = 0;
};

N64GameplayHudLayout BuildCanonicalN64GameplayHudLayout() noexcept;

struct N64GameplayHudBuildStats {
    std::uint32_t primitives_emitted = 0;
    std::uint32_t missing_asset_bindings = 0;
    std::uint32_t unknown_content_values = 0;
};

N64GameplayHudBuildStats AppendN64GameplayHudPresentation(
    const UiHudContentSnapshot& content,
    const Oot3dHudAssetCatalog& assets,
    const N64GameplayHudLayout& layout,
    std::vector<UiPrimitive>& output);

bool IsOot3dHudTextureIdentity(
    const UiTextureIdentity& texture) noexcept;

constexpr std::uint16_t Oot3dHudHeartVariant(
    std::uint8_t fraction_units, bool double_defense) noexcept {
    return static_cast<std::uint16_t>(fraction_units) |
           (double_defense ? 0x100U : 0U);
}

constexpr std::uint16_t Oot3dHudDigitVariant(
    std::uint8_t counter_style, std::uint8_t selection_variant,
    std::uint8_t digit) noexcept {
    return static_cast<std::uint16_t>(counter_style) * 20U +
           static_cast<std::uint16_t>(selection_variant) * 10U + digit;
}

} // namespace oot3d::ui
