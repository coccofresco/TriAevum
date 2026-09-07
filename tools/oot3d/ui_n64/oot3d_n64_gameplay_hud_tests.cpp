#include "oot3d_n64_gameplay_hud.h"

#include "oot3d_native_hud_atlas_regions.h"

#include "oot3d_ui/ui_content_value.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_n64_gameplay_hud_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

oot3d::ui::UiTextureIdentity Texture(std::string_view name) {
    oot3d::ui::UiTextureIdentity result;
    result.semantic_name = std::string(name);
    return result;
}

void AddVisual(oot3d::ui::Oot3dHudAssetCatalog& catalog,
               oot3d::ui::Oot3dHudAssetKind kind,
               std::uint16_t variant) {
    using namespace oot3d::ui;
    std::string error;
    Require(catalog.Add({{kind, variant},
                         Texture("oot3d/native/hud_all00.ctxb"),
                         {0.0F, 0.0F, 16.0F, 16.0F}, 256U, 256U},
                        &error),
            error);
}

} // namespace

int main() {
    using namespace oot3d::ui;

    Oot3dHudTextureSources sources;
    sources.pause_top_page =
        Texture("oot3d/native/pause_shared/pause_top_page");
    sources.item_icons =
        Texture("oot3d/native/pause_shared/item_icons");
    sources.number_glyphs =
        Texture("oot3d/native/pause_shared/number_glyphs");
    Oot3dHudAssetCatalog catalog =
        BuildVerifiedOot3dHudAssetCatalog(sources);
    Require(catalog.Size() > 185U,
            "verified OoT3D HUD/item/digit regions were not imported");

    const auto* full_heart = catalog.Find(
        {Oot3dHudAssetKind::Heart, Oot3dHudHeartVariant(16U, false)});
    const auto* half_heart = catalog.Find(
        {Oot3dHudAssetKind::Heart, Oot3dHudHeartVariant(8U, false)});
    const auto* defense_heart = catalog.Find(
        {Oot3dHudAssetKind::Heart, Oot3dHudHeartVariant(16U, true)});
    Require(full_heart != nullptr && full_heart->atlas_pixels.x == 240.0F &&
                full_heart->atlas_pixels.y == 144.0F &&
                half_heart != nullptr &&
                half_heart->atlas_pixels.y == 112.0F &&
                defense_heart != nullptr &&
                defense_heart->atlas_pixels.y == 64.0F,
            "native health-meter UV buckets were not imported exactly");
    const auto* rupee =
        catalog.Find({Oot3dHudAssetKind::RupeeIcon, 0U});
    const auto* key =
        catalog.Find({Oot3dHudAssetKind::SmallKeyIcon, 0U});
    Require(rupee != nullptr && rupee->atlas_pixels.x == 128.0F &&
                rupee->atlas_pixels.y == 96.0F && key != nullptr &&
                key->atlas_pixels.x == 152.0F &&
                key->atlas_pixels.y == 96.0F,
            "native rupee/key UV table entries were not imported exactly");

    std::string error;
    Require(!catalog.Add({{Oot3dHudAssetKind::RupeeIcon, 0U},
                          Texture("n64/interface/rupee"),
                          {0.0F, 0.0F, 16.0F, 16.0F}, 16U, 16U},
                         &error) &&
                error.find("oot3d namespace") != std::string::npos,
            "catalog accepted an N64 HUD texture fallback");

    AddVisual(catalog, Oot3dHudAssetKind::MagicFrame, 0U);
    AddVisual(catalog, Oot3dHudAssetKind::MagicFill, 0U);
    for (std::uint16_t button = 0; button < kOot3dButtonItemCount;
         ++button) {
        AddVisual(catalog, Oot3dHudAssetKind::ButtonFace, button);
    }

    UiHudContentSnapshot content;
    content.visibility.current = KnownUiContentValue(
        HudVisibilityMode::HUD_VISIBILITY_ALL);
    content.health.current_units = KnownUiContentValue<std::int16_t>(40);
    content.health.capacity_hearts = KnownUiContentValue<std::uint16_t>(3);
    content.health.double_defense_acquired = KnownUiContentValue(false);
    content.magic.acquired = KnownUiContentValue(true);
    content.magic.double_magic_acquired = KnownUiContentValue(false);
    content.magic.current = KnownUiContentValue<std::int8_t>(24);
    content.magic.capacity = KnownUiContentValue<std::int16_t>(48);
    content.counters.rupees = KnownUiContentValue<std::int16_t>(37);
    content.counters.current_dungeon_keys =
        KnownUiContentValue<std::int8_t>(2);

    constexpr std::array<ItemId, kOot3dButtonItemCount> items = {
        ItemId::ITEM_SWORD_KOKIRI, ItemId::ITEM_DEKU_STICK,
        ItemId::ITEM_DEKU_NUT, ItemId::ITEM_BOMB,
        ItemId::ITEM_SLINGSHOT};
    constexpr std::array<std::int8_t, kOot3dButtonItemCount> ammo = {
        10, 2, 3, 4, 5};
    for (std::size_t index = 0; index < content.buttons.size(); ++index) {
        content.buttons[index].button = static_cast<Oot3dButton>(index);
        content.buttons[index].item_id = KnownUiContentValue(items[index]);
        content.buttons[index].ammo = KnownUiContentValue(ammo[index]);
        content.buttons[index].status = KnownUiContentValue(
            index == 2U ? ButtonStatus::BTN_DISABLED
                        : ButtonStatus::BTN_ENABLED);
    }

    std::vector<UiPrimitive> primitives;
    const N64GameplayHudBuildStats stats =
        AppendN64GameplayHudPresentation(
            content, catalog, BuildCanonicalN64GameplayHudLayout(),
            primitives);
    Require(stats.primitives_emitted == primitives.size() &&
                stats.missing_asset_bindings == 0U &&
                stats.unknown_content_values == 0U,
            "complete typed HUD fixture did not build cleanly");
    Require(primitives.size() == 27U,
            "typed HUD fixture emitted an unexpected primitive count");
    Require(std::all_of(
                primitives.begin(), primitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.subsystem == UiSubsystem::GameplayHud &&
                           IsOot3dHudTextureIdentity(primitive.texture) &&
                           primitive.uv.width > 0.0F &&
                           primitive.uv.height > 0.0F;
                }),
            "HUD presenter emitted a foreign subsystem or non-OoT3D asset");
    Require(std::count_if(
                primitives.begin(), primitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.role == UiPrimitiveRole::AssignedItem;
                }) == static_cast<std::ptrdiff_t>(kOot3dButtonItemCount),
            "HUD presenter lost one of the five OoT3D item lanes");
    Require(std::any_of(
                primitives.begin(), primitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.role == UiPrimitiveRole::AssignedItem &&
                           primitive.source_quad == 2U &&
                           std::abs(primitive.color.alpha - 70.0F / 255.0F) <
                               0.0001F;
                }),
            "disabled OoT3D button did not use the native/N64 0x46 alpha");

    UiHudContentSnapshot hidden = content;
    hidden.visibility.current = KnownUiContentValue(
        HudVisibilityMode::HUD_VISIBILITY_NOTHING_INSTANT);
    std::vector<UiPrimitive> hidden_primitives;
    const auto hidden_stats = AppendN64GameplayHudPresentation(
        hidden, catalog, BuildCanonicalN64GameplayHudLayout(),
        hidden_primitives);
    Require(hidden_primitives.empty() &&
                hidden_stats.primitives_emitted == 0U,
            "hidden HUD visibility mode emitted presentation");

    std::cout << "oot3d_n64_gameplay_hud_tests: ok\n";
    return 0;
}
