#pragma once

// The in-game menu's texture rows (Graphics tab > Textures, UX plan WP-07),
// apart from any owner: the host binds them to the graphics settings owner,
// the same one F1 > Materials > Texture packs uses (oot3d_in_game_menu.cpp).
// Header-only; nothing here keeps a value.

#include "oot3d_menu_model.h"

#include <functional>
#include <utility>

namespace Oot3dNativeGame::Menu {

struct TextureRowBindings {
    // GraphicsSettings::TexturePacks.Azahar.LoadCustomTextures, now and when
    // the menu opened (the "modified" mark compares the two).
    std::function<bool()> LoadCustomTextures;
    std::function<bool()> BaselineLoadCustomTextures;
    // Applies the value through the owner. keepPreset: graphics presets
    // preserve texture packs, so this row never makes the preset Custom.
    std::function<WriteResult(bool value, bool keepPreset)> SetLoadCustomTextures;
};

inline constexpr const char* kCustomTexturesHelp =
    "Loads your own texture pack from the custom texture folder. Forge > Enhancements imports a pack; "
    "the folders are in F1 > Materials > Texture packs.";

// The Textures heading and its "Custom textures" toggle, appended to `tab`.
// Without bindings nothing is appended.
inline void AppendTextureRows(Tab& tab, const TextureRowBindings& bindings) {
    if (!bindings.LoadCustomTextures || !bindings.SetLoadCustomTextures) return;
    Row heading;
    heading.Key = "Textures";
    heading.Kind = RowKind::Heading;
    tab.Rows.push_back(std::move(heading));

    Row row;
    row.Key = "Custom textures";
    row.Kind = RowKind::Toggle;
    row.Help = kCustomTexturesHelp;
    const auto read = bindings.LoadCustomTextures;
    row.Read = [read] { return read() ? 1 : 0; };
    const auto set = bindings.SetLoadCustomTextures;
    row.Write = [set](int value) { return set(value != 0, true); };
    const auto baseline = bindings.BaselineLoadCustomTextures;
    row.Modified = [read, baseline] { return read() != (baseline ? baseline() : false); };
    tab.Rows.push_back(std::move(row));
}

} // namespace Oot3dNativeGame::Menu
