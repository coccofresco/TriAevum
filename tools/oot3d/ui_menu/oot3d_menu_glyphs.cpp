#include "oot3d_menu_glyphs.h"

#include "oot3d_ui/ui_contract_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>

namespace Oot3dNativeGame::Menu {
namespace {

using oot3d::ui::UiColor;
using oot3d::ui::UiPrimitive;

// The atlas: icons in 64-texel cells (eight a row, two rows), the frames in
// 128 x 64 cells below them (a 2:1 shape drawn in three slices: two caps and
// a stretched middle, for any label width).
constexpr int kCell = 64;
constexpr int kAtlasWidth = 512;
constexpr int kAtlasHeight = 256;
constexpr int kFrameRow = 2 * kCell;
// Shapes are laid out in units of the cell's height, with this margin.
constexpr float kMargin = 2.0F / static_cast<float>(kCell);
// Glyph height and side bearing, in multiples of the text size.
constexpr float kHeight = 1.06F;
constexpr float kBearing = 0.07F;
// The visual centre of mixed-case text sits near 0.36 em above the baseline.
constexpr float kCentre = 0.36F;

struct Point {
    float X = 0.0F;
    float Y = 0.0F;
};

float Length(float x, float y) noexcept { return std::sqrt(x * x + y * y); }

float Circle(Point p, Point centre, float radius) noexcept { return Length(p.X - centre.X, p.Y - centre.Y) - radius; }

float Segment(Point p, Point a, Point b) noexcept {
    const float abX = b.X - a.X, abY = b.Y - a.Y;
    const float apX = p.X - a.X, apY = p.Y - a.Y;
    const float lengthSquared = abX * abX + abY * abY;
    const float t = lengthSquared > 0.0F ? std::clamp((apX * abX + apY * abY) / lengthSquared, 0.0F, 1.0F) : 0.0F;
    return Length(apX - abX * t, apY - abY * t);
}

float RoundedBox(Point p, Point centre, float halfX, float halfY, float radius) noexcept {
    const float qX = std::abs(p.X - centre.X) - (halfX - radius);
    const float qY = std::abs(p.Y - centre.Y) - (halfY - radius);
    return Length(std::max(qX, 0.0F), std::max(qY, 0.0F)) + std::min(std::max(qX, qY), 0.0F) - radius;
}

// Signed distance to a closed polygon (negative inside).
float Polygon(Point p, std::span<const Point> vertices) noexcept {
    float distance = 1e9F;
    bool inside = false;
    for (std::size_t index = 0, previous = vertices.size() - 1; index < vertices.size(); previous = index++) {
        const auto& a = vertices[previous];
        const auto& b = vertices[index];
        distance = std::min(distance, Segment(p, a, b));
        if (((a.Y > p.Y) != (b.Y > p.Y)) && p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X) inside = !inside;
    }
    return inside ? -distance : distance;
}

// An outline of half width `half` along the shape's edge.
float Outline(float distance, float half) noexcept { return std::abs(distance) - half; }
// An outline inside the edge, so the shape keeps its outer size.
float InnerOutline(float distance, float half) noexcept { return std::abs(distance + half) - half; }
// The shape with a hole cut out.
float Cut(float shape, float hole) noexcept { return std::max(shape, -hole); }

float Coverage(float distance) noexcept {
    return std::clamp(0.5F - distance * static_cast<float>(kCell), 0.0F, 1.0F);
}

constexpr Point kCentrePoint{0.5F, 0.5F};
constexpr float kDiscRadius = 0.5F - kMargin;
constexpr float kStroke = 0.05F;

float Disc(Point p) noexcept { return Circle(p, kCentrePoint, kDiscRadius); }

// Coverage of an icon at p (unit cell).
float IconCoverage(GlyphIcon icon, Point p) noexcept {
    switch (icon) {
    case GlyphIcon::PsCross: {
        const float cross = std::min(Segment(p, {0.31F, 0.31F}, {0.69F, 0.69F}), Segment(p, {0.69F, 0.31F}, {0.31F, 0.69F}));
        return Coverage(Cut(Disc(p), cross - kStroke * 1.15F));
    }
    case GlyphIcon::PsCircle:
        return Coverage(Cut(Disc(p), Outline(Circle(p, kCentrePoint, 0.2F), kStroke * 1.1F)));
    case GlyphIcon::PsSquare:
        return Coverage(Cut(Disc(p), Outline(RoundedBox(p, kCentrePoint, 0.18F, 0.18F, 0.02F), kStroke)));
    case GlyphIcon::PsTriangle: {
        constexpr std::array<Point, 3> triangle{Point{0.5F, 0.27F}, Point{0.73F, 0.66F}, Point{0.27F, 0.66F}};
        return Coverage(Cut(Disc(p), Outline(Polygon(p, triangle), kStroke)));
    }
    case GlyphIcon::FaceSouth:
    case GlyphIcon::FaceEast:
    case GlyphIcon::FaceWest:
    case GlyphIcon::FaceNorth: {
        constexpr std::array<Point, 4> places{Point{0.5F, 0.79F}, Point{0.79F, 0.5F}, Point{0.21F, 0.5F},
                                              Point{0.5F, 0.21F}};
        const auto lit = static_cast<std::size_t>(icon) - static_cast<std::size_t>(GlyphIcon::FaceSouth);
        float coverage = 0.0F;
        for (std::size_t index = 0; index < places.size(); ++index) {
            const float button = Circle(p, places[index], 0.17F);
            coverage = std::max(coverage, Coverage(index == lit ? button : InnerOutline(button, 0.035F)));
        }
        return coverage;
    }
    case GlyphIcon::DpadHorizontal:
    case GlyphIcon::DpadVertical: {
        const float across = RoundedBox(p, kCentrePoint, 0.47F, 0.15F, 0.05F);
        const float along = RoundedBox(p, kCentrePoint, 0.15F, 0.47F, 0.05F);
        const float lit = icon == GlyphIcon::DpadHorizontal ? across : along;
        const float dim = icon == GlyphIcon::DpadHorizontal ? along : across;
        return std::max(Coverage(lit), 0.4F * Coverage(dim));
    }
    case GlyphIcon::Tap: {
        // A fingertip and the ripple of its touch.
        const float tip = Circle(p, {0.5F, 0.5F}, 0.17F);
        const float ripple = Outline(Circle(p, {0.5F, 0.5F}, 0.36F), 0.035F);
        return std::max(Coverage(tip), 0.6F * Coverage(ripple));
    }
    case GlyphIcon::Home: {
        constexpr std::array<Point, 7> house{Point{0.5F, 0.23F},  Point{0.77F, 0.48F}, Point{0.69F, 0.48F},
                                             Point{0.69F, 0.76F}, Point{0.31F, 0.76F}, Point{0.31F, 0.48F},
                                             Point{0.23F, 0.48F}};
        return Coverage(Cut(Disc(p), Polygon(p, house)));
    }
    case GlyphIcon::Count:
        break;
    }
    return 0.0F;
}

// Coverage of a frame at p (a 2 x 1 cell).
float FrameCoverage(GlyphFrame frame, Point p) noexcept {
    const float pill = Segment(p, {0.5F, 0.5F}, {1.5F, 0.5F}) - kDiscRadius;
    switch (frame) {
    case GlyphFrame::Solid:
        return Coverage(pill);
    case GlyphFrame::Key: {
        const float key = RoundedBox(p, {1.0F, 0.5F}, 1.0F - kMargin, 0.5F - kMargin, 0.2F);
        return std::max(0.18F * Coverage(key), Coverage(InnerOutline(key, 0.045F)));
    }
    case GlyphFrame::Stick:
        return std::max(0.18F * Coverage(pill), Coverage(InnerOutline(pill, 0.055F)));
    case GlyphFrame::Count:
        break;
    }
    return 0.0F;
}

GlyphAtlas BuildAtlas() {
    GlyphAtlas atlas;
    atlas.Width = kAtlasWidth;
    atlas.Height = kAtlasHeight;
    atlas.Coverage.assign(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight, 0U);
    const auto paint = [&](int left, int top, int width, const std::function<float(Point)>& coverage) {
        for (int y = 0; y < kCell; ++y) {
            for (int x = 0; x < width; ++x) {
                const Point p{(static_cast<float>(x) + 0.5F) / kCell, (static_cast<float>(y) + 0.5F) / kCell};
                const float value = std::clamp(coverage(p), 0.0F, 1.0F);
                atlas.Coverage[static_cast<std::size_t>(top + y) * kAtlasWidth + static_cast<std::size_t>(left + x)] =
                    static_cast<std::uint8_t>(std::lround(value * 255.0F));
            }
        }
    };
    for (std::size_t index = 0; index < static_cast<std::size_t>(GlyphIcon::Count); ++index) {
        const auto icon = static_cast<GlyphIcon>(index);
        paint(static_cast<int>(index % 8U) * kCell, static_cast<int>(index / 8U) * kCell, kCell,
              [icon](Point p) { return IconCoverage(icon, p); });
    }
    for (std::size_t index = 0; index < static_cast<std::size_t>(GlyphFrame::Count); ++index) {
        const auto frame = static_cast<GlyphFrame>(index);
        paint(static_cast<int>(index) * 2 * kCell, kFrameRow, 2 * kCell,
              [frame](Point p) { return FrameCoverage(frame, p); });
    }
    return atlas;
}

void EncodeUtf8(std::uint32_t code, std::string& out) {
    // Private Use Area codepoints are three bytes.
    out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
}

float GlyphTop(float baselineY, float size) noexcept { return baselineY - kCentre * size - kHeight * size * 0.5F; }

void Quad(std::vector<UiPrimitive>& output, float x, float y, float width, float height, float u, float v, float uWidth,
          float vHeight, const UiColor& color, std::uint32_t layer) {
    if (width <= 0.0F || height <= 0.0F) return;
    UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
    primitive.texture.semantic_name = std::string(kGlyphTexture);
    primitive.destination = {x, y, width, height};
    primitive.uv = {u / kAtlasWidth, v / kAtlasHeight, uWidth / kAtlasWidth, vHeight / kAtlasHeight};
    primitive.color = color;
    primitive.layer = layer;
    primitive.texture_alpha_only = true;
    output.push_back(std::move(primitive));
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

UiColor GlyphFrameLabelColor(GlyphFrame frame, const UiColor& color) noexcept {
    if (frame != GlyphFrame::Solid) return color;
    return {0.07F, 0.07F, 0.05F, color.alpha};
}

void AppendGlyphIcon(std::vector<UiPrimitive>& output, GlyphIcon icon, float x, float baselineY, float size,
                     const UiColor& color, std::uint32_t layer) {
    const auto index = static_cast<int>(icon);
    const float height = kHeight * size;
    Quad(output, x + kBearing * size, GlyphTop(baselineY, size), height, height,
         static_cast<float>((index % 8) * kCell), static_cast<float>((index / 8) * kCell), kCell, kCell, color, layer);
}

void AppendGlyphFrame(std::vector<UiPrimitive>& output, GlyphFrame frame, float x, float baselineY, float size,
                      float advance, const UiColor& color, std::uint32_t layer) {
    const float height = kHeight * size;
    const float width = std::max(height, advance - 2.0F * kBearing * size);
    const float left = x + kBearing * size;
    const float top = GlyphTop(baselineY, size);
    const float u = static_cast<float>(static_cast<int>(frame) * 2 * kCell);
    constexpr float v = kFrameRow;
    constexpr float cap = kCell * 0.5F;
    // The caps (half the height each) and the middle stretched between them;
    // the middle samples the straight part of the shape.
    Quad(output, left, top, height * 0.5F, height, u, v, cap, kCell, color, layer);
    Quad(output, left + height * 0.5F, top, width - height, height, u + kCell - 8.0F, v, 16.0F, kCell, color, layer);
    Quad(output, left + width - height * 0.5F, top, height * 0.5F, height, u + 2.0F * kCell - cap, v, cap, kCell, color,
         layer);
}

const GlyphAtlas& MenuGlyphAtlas() {
    static const GlyphAtlas atlas = BuildAtlas();
    return atlas;
}

} // namespace Oot3dNativeGame::Menu
