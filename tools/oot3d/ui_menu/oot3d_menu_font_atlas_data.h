#pragma once

// The baked in-game menu fonts (tools/oot3d/ui_menu/font): Fira Sans
// Condensed, SIL Open Font License 1.1, rasterized once into coverage atlases
// stored as PNG with their glyph tables. The generator writes
// oot3d_menu_font_atlas_data.cpp; nothing here reads a font file at run time.

#include <cstddef>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame::MenuFontData {

struct MenuGlyphRecord {
    std::uint32_t Codepoint;
    // Atlas cell (pixels) and the glyph's offset from the pen position: Left
    // from the pen, Top from the baseline (negative above it).
    std::uint16_t X, Y, Width, Height;
    std::int16_t Left, Top;
    float Advance;
};

struct MenuFontAtlasRecord {
    std::span<const unsigned char> Png;
    std::uint32_t Width, Height;
    float PixelSize, Ascent, Descent;
    std::span<const MenuGlyphRecord> Glyphs;
};

extern const MenuFontAtlasRecord kBodyAtlas;
extern const MenuFontAtlasRecord kHeadingAtlas;

} // namespace Oot3dNativeGame::MenuFontData
