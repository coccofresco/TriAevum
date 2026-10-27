#include "oot3d_menu_font.h"

#include "oot3d_menu_button_glyphs.h"
#include "oot3d_menu_glyphs.h"
#include "oot3d_ui/ui_contract_types.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>

namespace Oot3dNativeGame::Menu {
namespace {

const MenuFontData::MenuFontAtlasRecord& Atlas(FontStyle font) noexcept {
    return font == FontStyle::Heading ? MenuFontData::kHeadingAtlas : MenuFontData::kBodyAtlas;
}

const MenuFontData::MenuGlyphRecord* FindGlyph(const MenuFontData::MenuFontAtlasRecord& atlas,
                                               std::uint32_t codepoint) noexcept {
    // The glyph table is sorted by codepoint.
    const auto found = std::lower_bound(
        atlas.Glyphs.begin(), atlas.Glyphs.end(), codepoint,
        [](const MenuFontData::MenuGlyphRecord& glyph, std::uint32_t code) { return glyph.Codepoint < code; });
    if (found != atlas.Glyphs.end() && found->Codepoint == codepoint) return &*found;
    return nullptr;
}

const MenuFontData::MenuGlyphRecord* GlyphOrFallback(const MenuFontData::MenuFontAtlasRecord& atlas,
                                                     std::uint32_t codepoint) noexcept {
    if (const auto* glyph = FindGlyph(atlas, codepoint)) return glyph;
    return FindGlyph(atlas, static_cast<std::uint32_t>('?'));
}

std::uint32_t UppercaseCodepoint(std::uint32_t code) noexcept {
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    return code;
}

void EncodeUtf8(std::uint32_t code, std::string& out) {
    if (code < 0x80U) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
}

// One unit of laid-out text: a letter of the font, a prompt icon, or a
// prompt frame with its label (the codepoints [First, End) of the line).
struct TextUnit {
    std::size_t First = 0;
    std::size_t End = 0;
    std::optional<GlyphIcon> Icon;
    std::optional<GlyphFrame> Frame;
};

std::vector<TextUnit> SplitUnits(const std::vector<std::uint32_t>& codes) {
    std::vector<TextUnit> units;
    for (std::size_t index = 0; index < codes.size();) {
        TextUnit unit;
        unit.First = index;
        if (const auto frame = GlyphFrameOpenFromCodepoint(codes[index])) {
            unit.Frame = frame;
            std::size_t end = index + 1;
            while (end < codes.size() && !IsGlyphFrameClose(codes[end])) ++end;
            unit.End = std::min(end + 1, codes.size());
        } else {
            unit.Icon = GlyphIconFromCodepoint(codes[index]);
            unit.End = index + 1;
        }
        index = unit.End;
        units.push_back(unit);
    }
    return units;
}

// A frame's label: the codepoints between its opening and closing ones.
std::vector<std::uint32_t> FrameLabel(const std::vector<std::uint32_t>& codes, const TextUnit& unit) {
    std::size_t end = unit.End;
    if (end > unit.First + 1 && IsGlyphFrameClose(codes[end - 1])) --end;
    return {codes.begin() + static_cast<std::ptrdiff_t>(unit.First + 1), codes.begin() + static_cast<std::ptrdiff_t>(end)};
}

float MeasurePlain(const MenuFontData::MenuFontAtlasRecord& atlas, float scale, const std::vector<std::uint32_t>& codes,
                   bool uppercase) noexcept {
    float width = 0.0F;
    for (auto code : codes) {
        if (uppercase) code = UppercaseCodepoint(code);
        if (const auto* glyph = GlyphOrFallback(atlas, code)) width += glyph->Advance * scale;
    }
    return width;
}

float FrameLabelWidth(float size, const std::vector<std::uint32_t>& label) noexcept {
    const auto& atlas = Atlas(FontStyle::Heading);
    return MeasurePlain(atlas, GlyphFrameLabelSize(size) / atlas.PixelSize, label, false);
}

float UnitAdvance(FontStyle font, float size, const std::vector<std::uint32_t>& codes, const TextUnit& unit,
                  bool uppercase) noexcept {
    if (unit.Icon) return GlyphIconAdvance(size);
    if (unit.Frame) return GlyphFrameAdvance(*unit.Frame, size, FrameLabelWidth(size, FrameLabel(codes, unit)));
    const auto& atlas = Atlas(font);
    const auto code = uppercase ? UppercaseCodepoint(codes[unit.First]) : codes[unit.First];
    const auto* glyph = GlyphOrFallback(atlas, code);
    return glyph != nullptr ? glyph->Advance * size / atlas.PixelSize : 0.0F;
}

float MeasureCodepoints(FontStyle font, float size, const std::vector<std::uint32_t>& codes, bool uppercase) {
    float width = 0.0F;
    for (const auto& unit : SplitUnits(codes)) width += UnitAdvance(font, size, codes, unit, uppercase);
    return width;
}

std::string EncodeLabel(const std::vector<std::uint32_t>& label) {
    std::string out;
    for (const auto code : label) EncodeUtf8(code, out);
    return out;
}

} // namespace

std::vector<std::uint32_t> DecodeUtf8(std::string_view utf8) {
    std::vector<std::uint32_t> codes;
    codes.reserve(utf8.size());
    for (std::size_t index = 0; index < utf8.size();) {
        const auto byte = static_cast<unsigned char>(utf8[index]);
        std::uint32_t code = byte;
        std::size_t length = 1;
        if (byte >= 0xF0U) {
            code = byte & 0x07U;
            length = 4;
        } else if (byte >= 0xE0U) {
            code = byte & 0x0FU;
            length = 3;
        } else if (byte >= 0xC0U) {
            code = byte & 0x1FU;
            length = 2;
        }
        if (index + length > utf8.size()) {
            codes.push_back(static_cast<std::uint32_t>('?'));
            break;
        }
        for (std::size_t extra = 1; extra < length; ++extra) {
            code = (code << 6U) | (static_cast<unsigned char>(utf8[index + extra]) & 0x3FU);
        }
        codes.push_back(code);
        index += length;
    }
    return codes;
}

std::string UppercaseUtf8(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (const auto code : DecodeUtf8(utf8)) EncodeUtf8(UppercaseCodepoint(code), out);
    return out;
}

FontMetrics MeasureFont(FontStyle font, float size) noexcept {
    const auto& atlas = Atlas(font);
    const float scale = size / atlas.PixelSize;
    return {atlas.Ascent * scale, atlas.Descent * scale};
}

float MeasureText(FontStyle font, float size, std::string_view utf8, bool uppercase) {
    return MeasureCodepoints(font, size, DecodeUtf8(utf8), uppercase);
}

float AppendText(std::vector<oot3d::ui::UiPrimitive>& output, const TextStyle& style, float x, float baselineY,
                 std::string_view utf8) {
    const auto& atlas = Atlas(style.Font);
    const float scale = style.Size / atlas.PixelSize;
    const auto codes = DecodeUtf8(utf8);
    const auto units = SplitUnits(codes);
    float width = 0.0F;
    for (const auto& unit : units) width += UnitAdvance(style.Font, style.Size, codes, unit, style.Uppercase);
    float pen = x;
    if (style.Align == TextAlign::Center) pen -= width * 0.5F;
    if (style.Align == TextAlign::Right) pen -= width;
    const float atlasWidth = static_cast<float>(atlas.Width);
    const float atlasHeight = static_cast<float>(atlas.Height);
    for (const auto& unit : units) {
        const float advance = UnitAdvance(style.Font, style.Size, codes, unit, style.Uppercase);
        if (unit.Icon) {
            AppendGlyphIcon(output, *unit.Icon, pen, baselineY, style.Size, style.Color, style.Layer);
        } else if (unit.Frame) {
            AppendGlyphFrame(output, *unit.Frame, pen, baselineY, style.Size, advance, style.Color, style.Layer);
            const TextStyle label{FontStyle::Heading, GlyphFrameLabelSize(style.Size),
                                  GlyphFrameLabelColor(*unit.Frame, style.Color), TextAlign::Center, false, style.Layer};
            AppendText(output, label, pen + advance * 0.5F, GlyphFrameLabelBaseline(baselineY, style.Size),
                       EncodeLabel(FrameLabel(codes, unit)));
        } else {
            auto code = codes[unit.First];
            if (style.Uppercase) code = UppercaseCodepoint(code);
            const auto* glyph = GlyphOrFallback(atlas, code);
            if (glyph != nullptr && glyph->Width != 0U && glyph->Height != 0U) {
                oot3d::ui::UiPrimitive primitive;
                primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
                primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
                primitive.texture.semantic_name =
                    std::string(style.Font == FontStyle::Heading ? kHeadingFontTexture : kBodyFontTexture);
                primitive.destination = {pen + static_cast<float>(glyph->Left) * scale,
                                         baselineY + static_cast<float>(glyph->Top) * scale,
                                         static_cast<float>(glyph->Width) * scale,
                                         static_cast<float>(glyph->Height) * scale};
                primitive.uv = {static_cast<float>(glyph->X) / atlasWidth, static_cast<float>(glyph->Y) / atlasHeight,
                                static_cast<float>(glyph->Width) / atlasWidth,
                                static_cast<float>(glyph->Height) / atlasHeight};
                primitive.color = style.Color;
                primitive.layer = style.Layer;
                primitive.texture_alpha_only = true;
                output.push_back(std::move(primitive));
            }
        }
        pen += advance;
    }
    return width;
}

std::vector<std::string> WrapText(FontStyle font, float size, std::string_view utf8, float maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0;
    const auto flush = [&] {
        lines.push_back(line);
        line.clear();
    };
    while (start <= utf8.size()) {
        std::size_t end = utf8.find_first_of(" \n", start);
        if (end == std::string_view::npos) end = utf8.size();
        const std::string_view word = utf8.substr(start, end - start);
        const bool newline = end < utf8.size() && utf8[end] == '\n';
        std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && MeasureText(font, size, candidate) > maxWidth) {
            flush();
            candidate = std::string(word);
        }
        line = std::move(candidate);
        if (newline) flush();
        if (end >= utf8.size()) break;
        start = end + 1;
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

std::string ElideText(FontStyle font, float size, std::string_view utf8, float maxWidth, bool uppercase) {
    if (MeasureText(font, size, utf8, uppercase) <= maxWidth) return std::string(utf8);
    const auto codes = DecodeUtf8(utf8);
    const std::string ellipsis = "\xE2\x80\xA6"; // U+2026
    const float ellipsisWidth = MeasureText(font, size, ellipsis, uppercase);
    std::string out;
    float width = 0.0F;
    // A prompt is kept whole or left out, never cut inside its label.
    for (const auto& unit : SplitUnits(codes)) {
        const float advance = UnitAdvance(font, size, codes, unit, uppercase);
        if (width + advance + ellipsisWidth > maxWidth) break;
        for (std::size_t index = unit.First; index < unit.End; ++index) EncodeUtf8(codes[index], out);
        width += advance;
    }
    return out + ellipsis;
}

bool MenuFontTextureProvider::IsMenuTexture(std::string_view semanticName) noexcept {
    return semanticName == kBodyFontTexture || semanticName == kHeadingFontTexture ||
           ButtonGlyphCellFromTextureName(semanticName).has_value();
}

bool MenuFontTextureProvider::Resolve(const oot3d::ui::UiTextureIdentity& identity,
                                      oot3d::ui::UiTexturePixels& pixels, std::string* error) const {
    if (!IsMenuTexture(identity.semantic_name)) {
        if (mNext == nullptr) {
            if (error != nullptr) *error = "no texture provider for " + identity.semantic_name;
            return false;
        }
        return mNext->Resolve(identity, pixels, error);
    }
    const auto whiteWithCoverage = [](const std::uint8_t* coverage, int width, int height) {
        oot3d::ui::UiTexturePixels built;
        built.width = static_cast<std::uint16_t>(width);
        built.height = static_cast<std::uint16_t>(height);
        built.rgba8.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        for (std::size_t texel = 0; texel < built.rgba8.size() / 4U; ++texel) {
            built.rgba8[texel * 4U] = 0xFFU;
            built.rgba8[texel * 4U + 1U] = 0xFFU;
            built.rgba8[texel * 4U + 2U] = 0xFFU;
            built.rgba8[texel * 4U + 3U] = coverage[texel];
        }
        return built;
    };
    if (const auto cell = ButtonGlyphCellFromTextureName(identity.semantic_name)) {
        const auto& atlas = ButtonGlyphAtlasForCell(*cell);
        pixels.width = atlas.Width;
        pixels.height = atlas.Height;
        pixels.rgba8 = atlas.Rgba8;
        return true;
    }
    std::scoped_lock lock(mMutex);
    const bool heading = identity.semantic_name == kHeadingFontTexture;
    auto& cached = heading ? mHeading : mBody;
    if (cached.rgba8.empty()) {
        const auto& atlas = heading ? MenuFontData::kHeadingAtlas : MenuFontData::kBodyAtlas;
        int width = 0, height = 0, channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(atlas.Png.data(), static_cast<int>(atlas.Png.size()), &width,
                                                 &height, &channels, 1);
        if (decoded == nullptr || width != static_cast<int>(atlas.Width) || height != static_cast<int>(atlas.Height)) {
            if (decoded != nullptr) stbi_image_free(decoded);
            if (error != nullptr) *error = "cannot decode the menu font atlas " + identity.semantic_name;
            return false;
        }
        cached = whiteWithCoverage(decoded, width, height);
        stbi_image_free(decoded);
    }
    pixels = cached;
    return true;
}

} // namespace Oot3dNativeGame::Menu
