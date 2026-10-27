#pragma once

// Button prompt glyphs for the in-game menu's text, drawn with the button
// glyph art (oot3d_menu_button_glyphs.h): black and white discs, pills,
// keycaps (with the key's conventional symbol where it has one), a stick
// ring, the PlayStation symbols, a D-pad, a tap and a house for HOME.
//
// Text carries the glyphs as Private Use Area codepoints, so the menu's text
// layout (oot3d_menu_font) measures, wraps, elides and draws them with the
// letters: an icon is one codepoint; a labelled button is an opening
// codepoint, its label and the closing codepoint (the label is drawn with
// the menu font, black, inside the white shape).

#include "oot3d_menu_button_glyphs.h"
#include "oot3d_ui/ui_primitives.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame::Menu {

// The menu's text draws from the 64-texel button glyph atlas.
inline constexpr std::uint32_t kGlyphCell = 64U;
inline constexpr std::string_view kGlyphTexture = "triaevum/button_glyphs/64";

using GlyphIcon = ButtonGlyphIcon;

enum class GlyphFrame : std::uint8_t {
    // A filled button: a disc for one letter, a pill for more ("LB").
    Solid,
    // A keyboard key.
    Key,
    // A stick pressed in: a disc with a ring.
    Stick,
    Count,
};

inline constexpr std::uint32_t kGlyphIconBase = 0xE000U;
inline constexpr std::uint32_t kGlyphFrameBase = 0xE100U;

[[nodiscard]] std::string GlyphIconText(GlyphIcon icon);
[[nodiscard]] std::string GlyphFrameText(GlyphFrame frame, std::string_view label);

[[nodiscard]] std::optional<GlyphIcon> GlyphIconFromCodepoint(std::uint32_t code) noexcept;
[[nodiscard]] std::optional<GlyphFrame> GlyphFrameOpenFromCodepoint(std::uint32_t code) noexcept;
[[nodiscard]] bool IsGlyphFrameClose(std::uint32_t code) noexcept;

// Metrics in canvas units for text of `size` (the font's pixel size).
[[nodiscard]] float GlyphIconAdvance(float size) noexcept;
[[nodiscard]] float GlyphFrameLabelSize(float size) noexcept;
[[nodiscard]] float GlyphFrameAdvance(GlyphFrame frame, float size, float labelWidth) noexcept;
// The label's baseline inside a frame drawn on `baselineY`.
[[nodiscard]] float GlyphFrameLabelBaseline(float baselineY, float size) noexcept;
// The label's colour: black on the white shape, with the text's alpha.
[[nodiscard]] oot3d::ui::UiColor GlyphFrameLabelColor(GlyphFrame frame, const oot3d::ui::UiColor& color) noexcept;

// Appends the shapes at the pen position `x` on the text's baseline; only
// the text colour's alpha is used (the glyphs are black and white).
void AppendGlyphIcon(std::vector<oot3d::ui::UiPrimitive>& output, GlyphIcon icon, float x, float baselineY,
                     float size, const oot3d::ui::UiColor& color, std::uint32_t layer);
void AppendGlyphFrame(std::vector<oot3d::ui::UiPrimitive>& output, GlyphFrame frame, float x, float baselineY,
                      float size, float advance, const oot3d::ui::UiColor& color, std::uint32_t layer);

} // namespace Oot3dNativeGame::Menu
