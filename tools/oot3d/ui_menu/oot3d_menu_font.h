#pragma once

// Text for the in-game menu as native UI primitives: one textured quad per
// glyph from the baked Fira Sans Condensed atlases (oot3d_menu_font_atlas_data),
// drawn by the integrated UI renderer like the HUD. The atlases reach the
// renderer through a texture provider that answers the menu's own texture
// names and hands every other name on.

#include "oot3d_menu_font_atlas_data.h"
#include "oot3d_n64_ui_renderer.h"
#include "oot3d_ui/ui_primitives.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame::Menu {

enum class FontStyle : std::uint8_t { Body, Heading };

inline constexpr std::string_view kBodyFontTexture = "triaevum/menu/font/body";
inline constexpr std::string_view kHeadingFontTexture = "triaevum/menu/font/heading";

enum class TextAlign : std::uint8_t { Left, Center, Right };

struct TextStyle {
    FontStyle Font = FontStyle::Body;
    float Size = 4.0F; // glyph pixel size in canvas units
    oot3d::ui::UiColor Color{};
    TextAlign Align = TextAlign::Left;
    bool Uppercase = false;
    std::uint32_t Layer = 0;
};

// Canvas-unit metrics of a style: the line box is Ascent + Descent high.
struct FontMetrics {
    float Ascent = 0.0F;
    float Descent = 0.0F;
    [[nodiscard]] float LineHeight() const noexcept { return Ascent + Descent; }
};

[[nodiscard]] FontMetrics MeasureFont(FontStyle font, float size) noexcept;
// Advance width of the text (UTF-8) in canvas units; unknown glyphs take the
// width of '?'.
[[nodiscard]] float MeasureText(FontStyle font, float size, std::string_view utf8, bool uppercase = false);
[[nodiscard]] std::vector<std::uint32_t> DecodeUtf8(std::string_view utf8);
[[nodiscard]] std::string UppercaseUtf8(std::string_view utf8);

// Appends the glyph quads of one line. `x` is the pen origin of the alignment
// (left edge, centre or right edge) and `baselineY` the baseline, in canvas
// units. Returns the advance width drawn.
float AppendText(std::vector<oot3d::ui::UiPrimitive>& output, const TextStyle& style, float x, float baselineY,
                 std::string_view utf8);
// Greedy word wrap to `maxWidth` canvas units.
[[nodiscard]] std::vector<std::string> WrapText(FontStyle font, float size, std::string_view utf8, float maxWidth);
// Text cut with an ellipsis when wider than `maxWidth`.
[[nodiscard]] std::string ElideText(FontStyle font, float size, std::string_view utf8, float maxWidth,
                                    bool uppercase = false);

// Resolves the menu font atlases (decoded once) and defers every other
// identity to the provider the renderer had.
class MenuFontTextureProvider final : public oot3d::ui::UiTextureProvider {
  public:
    explicit MenuFontTextureProvider(const oot3d::ui::UiTextureProvider* next) noexcept : mNext(next) {}
    bool Resolve(const oot3d::ui::UiTextureIdentity& identity, oot3d::ui::UiTexturePixels& pixels,
                 std::string* error) const override;
    std::uint64_t Revision() const noexcept override { return mNext != nullptr ? mNext->Revision() : 0U; }
    [[nodiscard]] static bool IsMenuTexture(std::string_view semanticName) noexcept;

  private:
    const oot3d::ui::UiTextureProvider* mNext = nullptr;
    mutable std::mutex mMutex;
    mutable oot3d::ui::UiTexturePixels mBody, mHeading;
};

} // namespace Oot3dNativeGame::Menu
