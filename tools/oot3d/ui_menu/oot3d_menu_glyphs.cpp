#include "oot3d_menu_glyphs.h"

#include "oot3d_ui/ui_contract_types.h"

#include <algorithm>

namespace Oot3dNativeGame::Menu {
namespace {

using oot3d::ui::UiColor;
using oot3d::ui::UiPrimitive;

// Glyph height and side bearing, in multiples of the text size.
constexpr float kHeight = 1.06F;
constexpr float kBearing = 0.07F;
// The visual centre of mixed-case text sits near 0.36 em above the baseline.
constexpr float kCentre = 0.36F;

void EncodeUtf8(std::uint32_t code, std::string& out) {
    // Private Use Area codepoints are three bytes.
    out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
}

float GlyphTop(float baselineY, float size) noexcept { return baselineY - kCentre * size - kHeight * size * 0.5F; }

// White, with the text's alpha: the art carries its own black.
UiColor White(const UiColor& color) noexcept { return {1.0F, 1.0F, 1.0F, color.alpha}; }

ButtonGlyphFrame ArtFrame(GlyphFrame frame) noexcept {
    switch (frame) {
    case GlyphFrame::Solid: return ButtonGlyphFrame::Button;
    case GlyphFrame::Key: return ButtonGlyphFrame::Key;
    case GlyphFrame::Stick: return ButtonGlyphFrame::Stick;
    case GlyphFrame::Count: break;
    }
    return ButtonGlyphFrame::Count;
}

void RetagForMenu(std::vector<UiPrimitive>& output, std::size_t start) {
    for (std::size_t index = start; index < output.size(); ++index)
        output[index].role = oot3d::ui::UiPrimitiveRole::PauseText;
}

} // namespace

std::string GlyphIconText(GlyphIcon icon) {
    std::string text;
    EncodeUtf8(kGlyphIconBase + static_cast<std::uint32_t>(icon), text);
    return text;
}

std::string GlyphFrameText(GlyphFrame frame, std::string_view label) {
    std::string text;
    EncodeUtf8(kGlyphFrameBase + 2U * static_cast<std::uint32_t>(frame), text);
    text += label;
    EncodeUtf8(kGlyphFrameBase + 2U * static_cast<std::uint32_t>(frame) + 1U, text);
    return text;
}

std::optional<GlyphIcon> GlyphIconFromCodepoint(std::uint32_t code) noexcept {
    if (code < kGlyphIconBase || code >= kGlyphIconBase + static_cast<std::uint32_t>(GlyphIcon::Count))
        return std::nullopt;
    return static_cast<GlyphIcon>(code - kGlyphIconBase);
}

std::optional<GlyphFrame> GlyphFrameOpenFromCodepoint(std::uint32_t code) noexcept {
    if (code < kGlyphFrameBase || code >= kGlyphFrameBase + 2U * static_cast<std::uint32_t>(GlyphFrame::Count) ||
        (code - kGlyphFrameBase) % 2U != 0U)
        return std::nullopt;
    return static_cast<GlyphFrame>((code - kGlyphFrameBase) / 2U);
}

bool IsGlyphFrameClose(std::uint32_t code) noexcept {
    return code >= kGlyphFrameBase && code < kGlyphFrameBase + 2U * static_cast<std::uint32_t>(GlyphFrame::Count) &&
           (code - kGlyphFrameBase) % 2U == 1U;
}

float GlyphIconAdvance(float size) noexcept { return (kHeight + 2.0F * kBearing) * size; }

float GlyphFrameLabelSize(float size) noexcept { return 0.66F * size; }

float GlyphFrameAdvance(GlyphFrame frame, float size, float labelWidth) noexcept {
    const float height = kHeight * size;
    const float padding = frame == GlyphFrame::Key ? 0.5F * height : 0.55F * height;
    const float minimum = frame == GlyphFrame::Key ? 1.05F * height : height;
    return std::max(minimum, labelWidth + padding) + 2.0F * kBearing * size;
}

float GlyphFrameLabelBaseline(float baselineY, float size) noexcept {
    // Capitals of the label centred on the glyph's centre.
    return baselineY - kCentre * size + 0.35F * GlyphFrameLabelSize(size);
}

UiColor GlyphFrameLabelColor(GlyphFrame, const UiColor& color) noexcept { return {0.0F, 0.0F, 0.0F, color.alpha}; }

void AppendGlyphIcon(std::vector<UiPrimitive>& output, GlyphIcon icon, float x, float baselineY, float size,
                     const UiColor& color, std::uint32_t layer) {
    const std::size_t start = output.size();
    AppendButtonGlyphIcon(output, icon, x + kBearing * size, GlyphTop(baselineY, size), kHeight * size, White(color),
                          layer, kGlyphCell);
    RetagForMenu(output, start);
}

void AppendGlyphFrame(std::vector<UiPrimitive>& output, GlyphFrame frame, float x, float baselineY, float size,
                      float advance, const UiColor& color, std::uint32_t layer) {
    const float height = kHeight * size;
    const float width = std::max(height, advance - 2.0F * kBearing * size);
    const std::size_t start = output.size();
    AppendButtonGlyphFrame(output, ArtFrame(frame), x + kBearing * size, GlyphTop(baselineY, size), width, height,
                           White(color), layer, kGlyphCell);
    RetagForMenu(output, start);
}

} // namespace Oot3dNativeGame::Menu
