#include "three_ds_touch_skin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>

namespace ThreeDsRecomp::Input {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(TouchSkinShape::Count)> kShapeNames{
    "disc",    "ring",     "pill",     "pill_ring", "cross",   "cross_ring", "cross_arm",
    "arrow",   "glyph_a",  "glyph_b",  "glyph_x",   "glyph_y", "glyph_l",    "glyph_r",
    "glyph_zl", "glyph_zr", "glyph_start", "glyph_select", "glyph_home", "disc_soft", "pill_soft",
    "cross_soft", "gloss", "ring_thin", "rounded_box", "rounded_box_ring", "rounded_box_soft"};

constexpr int kSize = 256;
// Shapes are laid out in units of the texture's height.
constexpr float kPixel = 1.0F / static_cast<float>(kSize);
constexpr float kMargin = 2.0F * kPixel;
constexpr float kStroke = 0.045F;
constexpr float kGlyphHalfWidth = 0.065F;
// The soft shapes fade in over this distance from their edge.
constexpr float kSoftEdge = 0.14F;

struct Point {
    float X = 0.0F;
    float Y = 0.0F;
};

float Length(float x, float y) noexcept { return std::sqrt(x * x + y * y); }

float SegmentDistance(Point p, Point a, Point b) noexcept {
    const float abX = b.X - a.X, abY = b.Y - a.Y;
    const float apX = p.X - a.X, apY = p.Y - a.Y;
    const float lengthSquared = abX * abX + abY * abY;
    const float t = lengthSquared > 0.0F ? std::clamp((apX * abX + apY * abY) / lengthSquared, 0.0F, 1.0F) : 0.0F;
    return Length(apX - abX * t, apY - abY * t);
}

float PolylineDistance(Point p, std::span<const Point> points) noexcept {
    float distance = 1e9F;
    for (std::size_t index = 1; index < points.size(); ++index)
        distance = std::min(distance, SegmentDistance(p, points[index - 1], points[index]));
    return distance;
}

float RoundedBox(Point p, Point center, float halfX, float halfY, float radius) noexcept {
    const float qX = std::abs(p.X - center.X) - (halfX - radius);
    const float qY = std::abs(p.Y - center.Y) - (halfY - radius);
    return Length(std::max(qX, 0.0F), std::max(qY, 0.0F)) + std::min(std::max(qX, qY), 0.0F) - radius;
}

// Signed distance to a closed polygon (negative inside).
float Polygon(Point p, std::span<const Point> vertices) noexcept {
    float distance = 1e9F;
    bool inside = false;
    for (std::size_t index = 0, previous = vertices.size() - 1; index < vertices.size(); previous = index++) {
        const auto &a = vertices[previous];
        const auto &b = vertices[index];
        distance = std::min(distance, SegmentDistance(p, a, b));
        if (((a.Y > p.Y) != (b.Y > p.Y)) && p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X) inside = !inside;
    }
    return inside ? -distance : distance;
}

// Half a circle's outline, from its top to its bottom on the right.
void AppendRightArc(std::vector<Point> &points, Point center, float radius) {
    constexpr int kSteps = 16;
    for (int step = 0; step <= kSteps; ++step) {
        const float angle = -1.5707963F + 3.1415927F * static_cast<float>(step) / kSteps;
        points.push_back({center.X + radius * std::cos(angle), center.Y + radius * std::sin(angle)});
    }
}

using Strokes = std::vector<std::vector<Point>>;

// Letters in a unit box (y down), as centre lines of constant width.
Strokes LetterStrokes(char letter) {
    switch (letter) {
    case 'A':
        return {{{0.2F, 0.9F}, {0.5F, 0.1F}, {0.8F, 0.9F}}, {{0.31F, 0.62F}, {0.69F, 0.62F}}};
    case 'B': {
        std::vector<Point> top{{0.27F, 0.48F}, {0.27F, 0.1F}, {0.5F, 0.1F}};
        AppendRightArc(top, {0.5F, 0.29F}, 0.19F);
        top.push_back({0.27F, 0.48F});
        std::vector<Point> bottom{{0.27F, 0.48F}, {0.53F, 0.48F}};
        AppendRightArc(bottom, {0.53F, 0.69F}, 0.21F);
        bottom.push_back({0.27F, 0.9F});
        bottom.push_back({0.27F, 0.48F});
        return {top, bottom};
    }
    case 'X':
        return {{{0.22F, 0.1F}, {0.78F, 0.9F}}, {{0.78F, 0.1F}, {0.22F, 0.9F}}};
    case 'Y':
        return {{{0.22F, 0.1F}, {0.5F, 0.5F}, {0.78F, 0.1F}}, {{0.5F, 0.5F}, {0.5F, 0.9F}}};
    case 'L':
        return {{{0.3F, 0.1F}, {0.3F, 0.9F}, {0.76F, 0.9F}}};
    case 'R': {
        std::vector<Point> bowl{{0.27F, 0.9F}, {0.27F, 0.1F}, {0.52F, 0.1F}};
        AppendRightArc(bowl, {0.52F, 0.31F}, 0.21F);
        bowl.push_back({0.27F, 0.52F});
        return {bowl, {{0.47F, 0.52F}, {0.77F, 0.9F}}};
    }
    case 'Z':
        return {{{0.22F, 0.1F}, {0.78F, 0.1F}, {0.22F, 0.9F}, {0.78F, 0.9F}}};
    default:
        return {};
    }
}

// Letters side by side, each in its share of the unit box.
Strokes WordStrokes(std::string_view word) {
    Strokes result;
    const float share = 1.0F / static_cast<float>(word.size());
    // A narrower box keeps the letters' shape: each is squeezed less than its
    // share and centred in it.
    const float height = std::min(1.0F, share * 1.6F);
    const float top = (1.0F - height) * 0.5F;
    for (std::size_t index = 0; index < word.size(); ++index) {
        const float left = share * static_cast<float>(index) + (share - height * 0.8F) * 0.5F;
        for (auto stroke : LetterStrokes(word[index])) {
            for (auto &point : stroke) point = {left + point.X * height * 0.8F, top + point.Y * height};
            result.push_back(std::move(stroke));
        }
    }
    return result;
}

float StrokesDistance(Point p, const Strokes &strokes, float halfWidth) noexcept {
    float distance = 1e9F;
    for (const auto &stroke : strokes) distance = std::min(distance, PolylineDistance(p, stroke));
    return distance - halfWidth;
}

float CrossDistance(Point p) noexcept {
    const float arm = kTouchSkinCrossArmHalfWidth;
    const float half = 0.5F - kMargin;
    constexpr float radius = 0.06F;
    return std::min(RoundedBox(p, {0.5F, 0.5F}, half, arm, radius), RoundedBox(p, {0.5F, 0.5F}, arm, half, radius));
}

// Signed distance (texture-height units) of each shape at p.
std::function<float(Point)> ShapeDistance(TouchSkinShape shape) {
    const float radius = 0.5F - kMargin;
    switch (shape) {
    case TouchSkinShape::Disc:
        return [radius](Point p) { return Length(p.X - 0.5F, p.Y - 0.5F) - radius; };
    case TouchSkinShape::Ring:
    case TouchSkinShape::RingThin: {
        const float stroke = shape == TouchSkinShape::RingThin ? 0.023F : kStroke;
        return [radius, stroke](Point p) {
            return std::abs(Length(p.X - 0.5F, p.Y - 0.5F) - (radius - stroke * 0.5F)) - stroke * 0.5F;
        };
    }
    case TouchSkinShape::Pill:
        return [radius](Point p) { return SegmentDistance(p, {0.5F, 0.5F}, {1.5F, 0.5F}) - radius; };
    case TouchSkinShape::PillRing:
        return [radius](Point p) {
            constexpr float stroke = 0.060F;
            return std::abs(SegmentDistance(p, {0.5F, 0.5F}, {1.5F, 0.5F}) - (radius - stroke * 0.5F)) -
                   stroke * 0.5F;
        };
    case TouchSkinShape::Cross:
        return CrossDistance;
    case TouchSkinShape::RoundedBox:
        return [radius](Point p) { return RoundedBox(p, {0.5F, 0.5F}, radius, radius, 0.18F); };
    case TouchSkinShape::RoundedBoxRing:
        return [radius](Point p) {
            return std::abs(RoundedBox(p, {0.5F, 0.5F}, radius, radius, 0.18F) + kStroke * 0.5F) - kStroke * 0.5F;
        };
    case TouchSkinShape::CrossRing:
        return [](Point p) {
            constexpr float stroke = 0.023F;
            return std::abs(CrossDistance(p) + stroke * 0.5F) - stroke * 0.5F;
        };
    case TouchSkinShape::CrossArm:
        return [](Point p) {
            const float arm = kTouchSkinCrossArmHalfWidth;
            const float top = kMargin;
            return std::max(CrossDistance(p), RoundedBox(p, {0.5F, (top + 0.5F) * 0.5F}, arm, (0.5F - top) * 0.5F, 0.06F));
        };
    case TouchSkinShape::Arrow:
        return [](Point p) {
            constexpr std::array<Point, 3> triangle{{{0.5F, 0.22F}, {0.8F, 0.74F}, {0.2F, 0.74F}}};
            return Polygon(p, triangle) - 0.04F;
        };
    case TouchSkinShape::GlyphA:
    case TouchSkinShape::GlyphB:
    case TouchSkinShape::GlyphX:
    case TouchSkinShape::GlyphY:
    case TouchSkinShape::GlyphL:
    case TouchSkinShape::GlyphR: {
        constexpr std::string_view kLetters = "ABXYLR";
        const auto strokes = LetterStrokes(kLetters[static_cast<std::size_t>(shape) -
                                                    static_cast<std::size_t>(TouchSkinShape::GlyphA)]);
        return [strokes](Point p) { return StrokesDistance(p, strokes, kGlyphHalfWidth); };
    }
    case TouchSkinShape::GlyphZl:
    case TouchSkinShape::GlyphZr: {
        const auto strokes = WordStrokes(shape == TouchSkinShape::GlyphZl ? "ZL" : "ZR");
        return [strokes](Point p) { return StrokesDistance(p, strokes, kGlyphHalfWidth * 0.8F); };
    }
    case TouchSkinShape::GlyphStart:
        // START: a play triangle.
        return [](Point p) {
            constexpr std::array<Point, 3> triangle{{{0.36F, 0.24F}, {0.78F, 0.5F}, {0.36F, 0.76F}}};
            return Polygon(p, triangle) - 0.03F;
        };
    case TouchSkinShape::GlyphSelect: {
        // SELECT: three bars.
        const Strokes strokes{{{0.26F, 0.3F}, {0.74F, 0.3F}}, {{0.26F, 0.5F}, {0.74F, 0.5F}},
                              {{0.26F, 0.7F}, {0.74F, 0.7F}}};
        return [strokes](Point p) { return StrokesDistance(p, strokes, 0.055F); };
    }
    case TouchSkinShape::GlyphHome: {
        // HOME: a house.
        const Strokes strokes{{{0.16F, 0.52F}, {0.5F, 0.2F}, {0.84F, 0.52F}},
                              {{0.28F, 0.42F}, {0.28F, 0.82F}, {0.72F, 0.82F}, {0.72F, 0.42F}}};
        return [strokes](Point p) { return StrokesDistance(p, strokes, 0.055F); };
    }
    case TouchSkinShape::DiscSoft:
    case TouchSkinShape::PillSoft:
    case TouchSkinShape::CrossSoft:
    case TouchSkinShape::RoundedBoxSoft:
    case TouchSkinShape::Gloss:
    case TouchSkinShape::Count:
        break;
    }
    return [](Point) { return 1.0F; };
}

// The soft shapes' coverage (0..1) at p.
float SoftCoverage(TouchSkinShape shape, Point p) noexcept {
    const float radius = 0.5F - kMargin;
    float distance = 1.0F;
    float edge = kSoftEdge;
    switch (shape) {
    case TouchSkinShape::DiscSoft: distance = Length(p.X - 0.5F, p.Y - 0.5F) - radius; break;
    case TouchSkinShape::PillSoft: distance = SegmentDistance(p, {0.5F, 0.5F}, {1.5F, 0.5F}) - radius; break;
    case TouchSkinShape::CrossSoft: distance = CrossDistance(p); edge = kSoftEdge * 0.7F; break;
    case TouchSkinShape::RoundedBoxSoft:
        distance = RoundedBox(p, {0.5F, 0.5F}, radius, radius, 0.18F);
        break;
    case TouchSkinShape::Gloss:
        distance = (Length((p.X - 0.5F) / 0.34F, (p.Y - 0.3F) / 0.19F) - 1.0F) * 0.19F;
        edge = 0.12F;
        break;
    default: return 0.0F;
    }
    const float t = std::clamp(-distance / edge, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

bool IsSoft(TouchSkinShape shape) noexcept {
    return shape == TouchSkinShape::DiscSoft || shape == TouchSkinShape::PillSoft ||
           shape == TouchSkinShape::CrossSoft || shape == TouchSkinShape::RoundedBoxSoft || shape == TouchSkinShape::Gloss;
}

} // namespace

std::string TouchSkinSemanticName(TouchSkinShape shape) {
    if (shape == TouchSkinShape::Count) return {};
    return std::string(kTouchSkinSemanticPrefix) + std::string(kShapeNames[static_cast<std::size_t>(shape)]);
}

std::optional<TouchSkinShape> TouchSkinShapeFromSemanticName(std::string_view name) noexcept {
    if (!name.starts_with(kTouchSkinSemanticPrefix)) return std::nullopt;
    name.remove_prefix(kTouchSkinSemanticPrefix.size());
    for (std::size_t index = 0; index < kShapeNames.size(); ++index)
        if (kShapeNames[index] == name) return static_cast<TouchSkinShape>(index);
    return std::nullopt;
}

TouchSkinPixels RasterizeTouchSkinShape(TouchSkinShape shape) {
    TouchSkinPixels pixels;
    if (shape == TouchSkinShape::Count) return pixels;
    const bool pill = shape == TouchSkinShape::Pill || shape == TouchSkinShape::PillRing ||
                      shape == TouchSkinShape::PillSoft;
    pixels.Width = static_cast<std::uint16_t>(kSize);
    pixels.Height = static_cast<std::uint16_t>(pill ? kSize / 2 : kSize);
    pixels.Rgba8.assign(static_cast<std::size_t>(pixels.Width) * pixels.Height * 4U, 0xFFU);
    const auto distance = ShapeDistance(shape);
    // Pills span two units across their height; everything else one.
    const float unit = 1.0F / static_cast<float>(pixels.Height);
    const float edge = 1.5F * unit;
    for (int y = 0; y < pixels.Height; ++y) {
        for (int x = 0; x < pixels.Width; ++x) {
            const Point p{(static_cast<float>(x) + 0.5F) * unit, (static_cast<float>(y) + 0.5F) * unit};
            const float coverage =
                IsSoft(shape) ? SoftCoverage(shape, p) : std::clamp(0.5F - distance(p) / edge, 0.0F, 1.0F);
            pixels.Rgba8[(static_cast<std::size_t>(y) * pixels.Width + x) * 4U + 3U] =
                static_cast<std::uint8_t>(std::lround(coverage * 255.0F));
        }
    }
    return pixels;
}

} // namespace ThreeDsRecomp::Input
