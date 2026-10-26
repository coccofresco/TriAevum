#pragma once

// Button prompt glyphs for the in-game menu's text. The art is TriAevum's
// own: plain geometric shapes (discs, pills, keycaps, the PlayStation
// symbols as simple strokes, a D-pad, a tap, a house for HOME) described as
// signed distances and rasterized once into one coverage atlas at first use.
// No font, picture or third-party glyph set is involved, and no console
// logo is drawn.
//
// Text carries the glyphs as Private Use Area codepoints, so the menu's text
// layout (oot3d_menu_font) measures, wraps, elides and draws them with the
// letters: an icon is one codepoint; a labelled button is an opening
// codepoint, its label and the closing codepoint (the label is drawn with
// the menu font inside the shape).

#include "oot3d_ui/ui_primitives.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame::Menu {

inline constexpr std::string_view kGlyphTexture = "triaevum/menu/glyphs";

enum class GlyphIcon : std::uint8_t {
    PsCross,
    PsCircle,
    PsSquare,
    PsTriangle,
    // A pad's four face buttons by position, the named one filled.
    FaceSouth,
    FaceEast,
    FaceWest,
    FaceNorth,
    // The D-pad with its left and right (or up and down) arms lit.
    DpadHorizontal,
    DpadVertical,
    Tap,
    Home,
    Count,
};

enum class GlyphFrame : std::uint8_t {
    // A filled button: a disc for one letter, a pill for more ("LB").
    Solid,
    // A keyboard key.
    Key,
    // A stick pressed in: a ring.
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
// The label's colour: dark on a filled button, the text's own otherwise.
[[nodiscard]] oot3d::ui::UiColor GlyphFrameLabelColor(GlyphFrame frame, const oot3d::ui::UiColor& color) noexcept;

// Appends the shapes at the pen position `x` on the text's baseline.
void AppendGlyphIcon(std::vector<oot3d::ui::UiPrimitive>& output, GlyphIcon icon, float x, float baselineY,
                     float size, const oot3d::ui::UiColor& color, std::uint32_t layer);
void AppendGlyphFrame(std::vector<oot3d::ui::UiPrimitive>& output, GlyphFrame frame, float x, float baselineY,
                      float size, float advance, const oot3d::ui::UiColor& color, std::uint32_t layer);

// The atlas: one coverage byte per texel (white glyphs, tinted when drawn).
struct GlyphAtlas {
    std::uint16_t Width = 0;
    std::uint16_t Height = 0;
    std::vector<std::uint8_t> Coverage;
};

// Rasterized once; the same bytes on every call.
[[nodiscard]] const GlyphAtlas& MenuGlyphAtlas();

} // namespace Oot3dNativeGame::Menu
