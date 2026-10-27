#pragma once

// Button glyphs: the art of every button prompt TriAevum draws, in the
// in-game menu's text (oot3d_menu_glyphs.h) and on a title's HUD in place of
// the console's button letters (the input bound to that console button, in
// the prompt family of the device in use, Input's PromptSet).
//
// The art is TriAevum's own: discs, pills, keycaps, a stick ring, the
// PlayStation symbols as plain strokes, a D-pad, a mouse, a tap, a house and
// the keyboard's conventional key symbols (space bar, shift, return,
// backspace, tab, caps lock, arrows, page up / down, home, end, insert,
// delete), described as signed distances. No font, picture, console logo or
// third-party glyph set is used; a label inside a disc, pill or keycap is
// drawn with the menu's heading font.
//
// Black and white only: a white body with a black mark (letter, symbol or
// stroke) and a thin black edge, so a glyph reads on the dark menu panel and
// on any game picture, and a glyph drawn over a letter baked into the game's
// art hides it. The colours are baked into the atlas (straight-alpha RGBA);
// the atlas is made at run time in three sizes (32, 64 and 128 texels a cell)
// so the size drawn is never sampled from far more texels than it covers (the
// UI renderer has no mipmaps): crisp from 720p to 4K.

#include "oot3d_ui/ui_primitives.h"
#include "three_ds_input.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame::Menu {

inline constexpr std::string_view kButtonGlyphTexturePrefix = "triaevum/button_glyphs/";

enum class ButtonGlyphIcon : std::uint8_t {
    PsCross,
    PsCircle,
    PsSquare,
    PsTriangle,
    // The D-pad with one arm, or a pair of arms, filled.
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    DpadHorizontal,
    DpadVertical,
    // A mouse with one button filled.
    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseBack,
    MouseForward,
    // Touch: a fingertip, and HOME's house.
    Tap,
    Home,
    // Keycaps with the key's conventional symbol.
    KeySpace,
    KeyShift,
    KeyEnter,
    KeyBackspace,
    KeyTab,
    KeyCapsLock,
    KeyUp,
    KeyDown,
    KeyLeft,
    KeyRight,
    KeyPageUp,
    KeyPageDown,
    KeyHome,
    KeyEnd,
    KeyInsert,
    KeyDelete,
    Count,
};

// A shape with a text label inside, as wide as its label needs (never
// narrower than tall: one letter makes a disc or a square key).
enum class ButtonGlyphFrame : std::uint8_t {
    Button,
    Key,
    Stick,
    Count,
};

struct ButtonGlyph {
    bool Labelled = false;
    ButtonGlyphIcon Icon = ButtonGlyphIcon::Count;
    ButtonGlyphFrame Frame = ButtonGlyphFrame::Count;
    std::string Label;

    [[nodiscard]] static ButtonGlyph Symbol(ButtonGlyphIcon icon) { return {false, icon, ButtonGlyphFrame::Count, {}}; }
    [[nodiscard]] static ButtonGlyph Framed(ButtonGlyphFrame frame, std::string label) {
        return {true, ButtonGlyphIcon::Count, frame, std::move(label)};
    }
    bool operator==(const ButtonGlyph&) const = default;
};

// A pad button in its family's look. The face buttons are SDL's positional
// A / B / X / Y (south, east, west, north): PlayStation shows its symbols,
// every other family a disc with the letter (a Nintendo pad is mapped by its
// printed labels, as SDL does; a generic pad by SDL's names for the
// positions).
[[nodiscard]] ButtonGlyph GamepadButtonGlyph(ThreeDsRecomp::Input::PromptSet set,
                                             ThreeDsRecomp::Input::GamepadButton button);
// The key's conventional symbol (space bar, shift, return, backspace, tab,
// arrows), if it has one.
[[nodiscard]] std::optional<ButtonGlyphIcon> KeyboardKeySymbol(ThreeDsRecomp::Input::KeyboardKey key) noexcept;
// The key's printed name, for a key without a symbol ("Q", "1", "Esc").
[[nodiscard]] std::string KeyboardKeyLabel(ThreeDsRecomp::Input::KeyboardKey key);
// A keycap: the key's symbol, else its name.
[[nodiscard]] ButtonGlyph KeyboardKeyGlyph(ThreeDsRecomp::Input::KeyboardKey key);
[[nodiscard]] ButtonGlyph MouseButtonGlyph(ThreeDsRecomp::Input::MouseButton button);

// The input bound to one console button, in the family: a pad family shows
// the binding's pad button; the keyboard its first key, else its second,
// else its mouse button. Nothing when that device has no binding, and
// nothing for touch (the console's own label stays).
[[nodiscard]] std::optional<ButtonGlyph> ResolveButtonGlyph(ThreeDsRecomp::Input::PromptSet set,
                                                            const ThreeDsRecomp::Input::HostBinding& binding);

// The glyph's width for a height, in the same units.
[[nodiscard]] float ButtonGlyphWidth(const ButtonGlyph& glyph, float height);

// The atlas size for a glyph drawn `pixelHeight` framebuffer pixels high:
// the smallest cell that is at least as tall (32, 64 or 128 texels).
[[nodiscard]] std::uint32_t ButtonGlyphCellForPixels(float pixelHeight) noexcept;

struct ButtonGlyphColors {
    // Multiplies the baked white body (and its black marks stay black);
    // a label's ink.
    oot3d::ui::UiColor Body{1.0F, 1.0F, 1.0F, 1.0F};
    oot3d::ui::UiColor Ink{0.0F, 0.0F, 0.0F, 1.0F};
};

// Appends the glyph centred on (centerX, centerY), `height` canvas units
// high, with `alpha` on every part; `cell` is the atlas size to sample
// (ButtonGlyphCellForPixels).
void AppendButtonGlyph(std::vector<oot3d::ui::UiPrimitive>& output, const ButtonGlyph& glyph, float centerX,
                       float centerY, float height, float alpha, std::uint32_t layer, std::uint32_t cell,
                       const ButtonGlyphColors& colors = {});

// The shapes alone, for a caller that lays out and labels them itself (the
// menu's text): an icon in a `size` square at (left, top), a frame's three
// slices over `width` x `height`.
void AppendButtonGlyphIcon(std::vector<oot3d::ui::UiPrimitive>& output, ButtonGlyphIcon icon, float left, float top,
                           float size, const oot3d::ui::UiColor& color, std::uint32_t layer, std::uint32_t cell);
void AppendButtonGlyphFrame(std::vector<oot3d::ui::UiPrimitive>& output, ButtonGlyphFrame frame, float left,
                            float top, float width, float height, const oot3d::ui::UiColor& color,
                            std::uint32_t layer, std::uint32_t cell);

struct ButtonGlyphAtlas {
    std::uint16_t Width = 0;
    std::uint16_t Height = 0;
    std::uint16_t Cell = 0;
    // Straight-alpha RGBA: white body, black edge and marks.
    std::vector<std::uint8_t> Rgba8;
};

[[nodiscard]] std::string ButtonGlyphTextureName(std::uint32_t cell);
// The cell size a texture name stands for, if it is one of the atlases.
[[nodiscard]] std::optional<std::uint32_t> ButtonGlyphCellFromTextureName(std::string_view name) noexcept;
// Rasterized once per size; the same bytes on every call.
[[nodiscard]] const ButtonGlyphAtlas& ButtonGlyphAtlasForCell(std::uint32_t cell);
// Atlas layout, for tests: icons eight a row, then one row of 2:1 frames.
inline constexpr int kButtonGlyphIconsPerRow = 8;
inline constexpr int kButtonGlyphIconRows =
    (static_cast<int>(ButtonGlyphIcon::Count) + kButtonGlyphIconsPerRow - 1) / kButtonGlyphIconsPerRow;

} // namespace Oot3dNativeGame::Menu
