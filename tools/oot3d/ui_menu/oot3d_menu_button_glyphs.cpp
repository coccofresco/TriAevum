#include "oot3d_menu_button_glyphs.h"

#include "oot3d_menu_font.h"
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
using ThreeDsRecomp::Input::GamepadButton;
using ThreeDsRecomp::Input::KeyboardKey;
using ThreeDsRecomp::Input::MouseButton;
using ThreeDsRecomp::Input::PromptSet;

constexpr int kIconsPerRow = kButtonGlyphIconsPerRow;
constexpr int kIconRows = kButtonGlyphIconRows;
constexpr std::array<std::uint32_t, 3> kCells{32U, 64U, 128U};
// Shapes are laid out in units of the cell's height. The black edge is this
// wide around the white body.
constexpr float kEdge = 0.06F;
// The label's size and the room it keeps, in glyph heights.
constexpr float kButtonLetter = 0.60F;
constexpr float kButtonWord = 0.50F;
constexpr float kKeyLetter = 0.52F;
constexpr float kKeyWord = 0.42F;
constexpr float kStickLabel = 0.48F;

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

float Outline(float distance, float half) noexcept { return std::abs(distance) - half; }
float InnerOutline(float distance, float half) noexcept { return std::abs(distance + half) - half; }
float Cut(float shape, float hole) noexcept { return std::max(shape, -hole); }
float Union(float a, float b) noexcept { return std::min(a, b); }
float Line(Point p, Point a, Point b, float half) noexcept { return Segment(p, a, b) - half; }

// A filled arrow from its tail to its tip.
float Arrow(Point p, Point tail, Point tip, float half = 0.042F, float headLength = 0.16F,
            float headHalf = 0.13F) noexcept {
    const float dx = tip.X - tail.X, dy = tip.Y - tail.Y;
    const float length = Length(dx, dy);
    const float ux = dx / length, uy = dy / length;
    const Point base{tip.X - ux * headLength, tip.Y - uy * headLength};
    const std::array<Point, 3> head{tip, Point{base.X - uy * headHalf, base.Y + ux * headHalf},
                                    Point{base.X + uy * headHalf, base.Y - ux * headHalf}};
    return Union(Line(p, tail, {base.X + ux * 0.01F, base.Y + uy * 0.01F}, half), Polygon(p, head));
}

// Two signed distances: the whole glyph (black where only it covers) and its
// white body.
struct Shape {
    float Silhouette = 1.0F;
    float Body = 1.0F;
};

constexpr Point kCentre{0.5F, 0.5F};

// The black mark a disc icon cuts out of its white disc.
float DiscMark(ButtonGlyphIcon icon, Point p) noexcept {
    constexpr float stroke = 0.062F;
    switch (icon) {
    case ButtonGlyphIcon::PsCross:
        return Union(Segment(p, {0.34F, 0.34F}, {0.66F, 0.66F}), Segment(p, {0.66F, 0.34F}, {0.34F, 0.66F})) - stroke;
    case ButtonGlyphIcon::PsCircle:
        return Outline(Circle(p, kCentre, 0.17F), stroke);
    case ButtonGlyphIcon::PsSquare:
        return Outline(RoundedBox(p, kCentre, 0.155F, 0.155F, 0.02F), stroke);
    case ButtonGlyphIcon::PsTriangle: {
        constexpr std::array<Point, 3> triangle{Point{0.5F, 0.30F}, Point{0.70F, 0.645F}, Point{0.30F, 0.645F}};
        return Outline(Polygon(p, triangle), stroke);
    }
    case ButtonGlyphIcon::DpadUp:
    case ButtonGlyphIcon::DpadDown:
    case ButtonGlyphIcon::DpadLeft:
    case ButtonGlyphIcon::DpadRight:
    case ButtonGlyphIcon::DpadHorizontal:
    case ButtonGlyphIcon::DpadVertical: {
        const float across = RoundedBox(p, kCentre, 0.33F, 0.125F, 0.035F);
        const float along = RoundedBox(p, kCentre, 0.125F, 0.33F, 0.035F);
        const auto arm = [&](Point end) { return RoundedBox(p, end, 0.125F, 0.115F, 0.035F); };
        const Point up{0.5F, 0.285F}, down{0.5F, 0.715F}, left{0.285F, 0.5F}, right{0.715F, 0.5F};
        float lit = 1.0F;
        switch (icon) {
        case ButtonGlyphIcon::DpadUp: lit = arm(up); break;
        case ButtonGlyphIcon::DpadDown: lit = arm(down); break;
        case ButtonGlyphIcon::DpadLeft: lit = arm(left); break;
        case ButtonGlyphIcon::DpadRight: lit = arm(right); break;
        case ButtonGlyphIcon::DpadHorizontal: lit = Union(arm(left), arm(right)); break;
        default: lit = Union(arm(up), arm(down)); break;
        }
        return Union(InnerOutline(Union(across, along), 0.032F), lit);
    }
    case ButtonGlyphIcon::MouseLeft:
    case ButtonGlyphIcon::MouseRight:
    case ButtonGlyphIcon::MouseMiddle:
    case ButtonGlyphIcon::MouseBack:
    case ButtonGlyphIcon::MouseForward: {
        const float mouse = RoundedBox(p, {0.5F, 0.52F}, 0.195F, 0.29F, 0.185F);
        const float inset = mouse + 0.065F;
        const float wheel = RoundedBox(p, {0.5F, 0.35F}, 0.032F, 0.065F, 0.03F);
        float lit = 1.0F;
        switch (icon) {
        case ButtonGlyphIcon::MouseLeft: lit = std::max({inset, p.Y - 0.44F, p.X - 0.47F}); break;
        case ButtonGlyphIcon::MouseRight: lit = std::max({inset, p.Y - 0.44F, 0.53F - p.X}); break;
        case ButtonGlyphIcon::MouseMiddle: lit = wheel; break;
        case ButtonGlyphIcon::MouseBack: lit = RoundedBox(p, {0.272F, 0.62F}, 0.035F, 0.075F, 0.03F); break;
        default: lit = RoundedBox(p, {0.272F, 0.44F}, 0.035F, 0.075F, 0.03F); break;
        }
        return Union(Union(InnerOutline(mouse, 0.04F), InnerOutline(wheel, 0.025F)), lit);
    }
    case ButtonGlyphIcon::Tap:
        // A fingertip and the ripple of its touch.
        return Union(Circle(p, kCentre, 0.12F), Outline(Circle(p, kCentre, 0.26F), 0.032F));
    case ButtonGlyphIcon::Home: {
        constexpr std::array<Point, 7> house{Point{0.5F, 0.25F},  Point{0.75F, 0.48F}, Point{0.67F, 0.48F},
                                             Point{0.67F, 0.74F}, Point{0.33F, 0.74F}, Point{0.33F, 0.48F},
                                             Point{0.25F, 0.48F}};
        return Polygon(p, house);
    }
    default:
        break;
    }
    return 1.0F;
}

// The black symbol a keycap icon carries.
float KeyMark(ButtonGlyphIcon icon, Point p) noexcept {
    constexpr float stroke = 0.038F;
    switch (icon) {
    case ButtonGlyphIcon::KeySpace:
        // The space bar's open box.
        return Union(Union(Line(p, {0.30F, 0.47F}, {0.30F, 0.62F}, stroke), Line(p, {0.30F, 0.62F}, {0.70F, 0.62F}, stroke)),
                     Line(p, {0.70F, 0.62F}, {0.70F, 0.47F}, stroke));
    case ButtonGlyphIcon::KeyShift: {
        constexpr std::array<Point, 7> arrow{Point{0.5F, 0.27F},  Point{0.73F, 0.51F}, Point{0.6F, 0.51F},
                                             Point{0.6F, 0.72F},  Point{0.4F, 0.72F},  Point{0.4F, 0.51F},
                                             Point{0.27F, 0.51F}};
        return Outline(Polygon(p, arrow), 0.032F);
    }
    case ButtonGlyphIcon::KeyCapsLock: {
        constexpr std::array<Point, 7> arrow{Point{0.5F, 0.25F},  Point{0.72F, 0.47F}, Point{0.6F, 0.47F},
                                             Point{0.6F, 0.61F},  Point{0.4F, 0.61F},  Point{0.4F, 0.47F},
                                             Point{0.28F, 0.47F}};
        return Union(Outline(Polygon(p, arrow), 0.03F), Line(p, {0.4F, 0.72F}, {0.6F, 0.72F}, 0.035F));
    }
    case ButtonGlyphIcon::KeyEnter:
        return Union(Line(p, {0.69F, 0.29F}, {0.69F, 0.60F}, stroke), Arrow(p, {0.69F, 0.60F}, {0.27F, 0.60F}, stroke));
    case ButtonGlyphIcon::KeyBackspace:
    case ButtonGlyphIcon::KeyDelete: {
        // An arrow-shaped key cap with a cross in it, pointing left or right.
        const bool left = icon == ButtonGlyphIcon::KeyBackspace;
        const auto x = [left](float value) { return left ? value : 1.0F - value; };
        const std::array<Point, 5> body{Point{x(0.25F), 0.5F}, Point{x(0.42F), 0.32F}, Point{x(0.75F), 0.32F},
                                        Point{x(0.75F), 0.68F}, Point{x(0.42F), 0.68F}};
        const float cross = Union(Line(p, {x(0.51F), 0.43F}, {x(0.65F), 0.57F}, 0.03F),
                                  Line(p, {x(0.65F), 0.43F}, {x(0.51F), 0.57F}, 0.03F));
        return Union(Outline(Polygon(p, body), 0.03F), cross);
    }
    case ButtonGlyphIcon::KeyTab:
        return Union(Arrow(p, {0.27F, 0.5F}, {0.68F, 0.5F}, stroke), Line(p, {0.72F, 0.34F}, {0.72F, 0.66F}, stroke));
    case ButtonGlyphIcon::KeyUp: return Arrow(p, {0.5F, 0.73F}, {0.5F, 0.26F});
    case ButtonGlyphIcon::KeyDown: return Arrow(p, {0.5F, 0.27F}, {0.5F, 0.74F});
    case ButtonGlyphIcon::KeyLeft: return Arrow(p, {0.73F, 0.5F}, {0.26F, 0.5F});
    case ButtonGlyphIcon::KeyRight: return Arrow(p, {0.27F, 0.5F}, {0.74F, 0.5F});
    case ButtonGlyphIcon::KeyPageUp:
    case ButtonGlyphIcon::KeyPageDown: {
        // An arrow with a double stroke across its shaft.
        const bool up = icon == ButtonGlyphIcon::KeyPageUp;
        const auto y = [up](float value) { return up ? value : 1.0F - value; };
        return Union(Arrow(p, {0.5F, y(0.75F)}, {0.5F, y(0.25F)}, 0.035F),
                     Union(Line(p, {0.38F, y(0.55F)}, {0.62F, y(0.55F)}, 0.03F),
                           Line(p, {0.38F, y(0.65F)}, {0.62F, y(0.65F)}, 0.03F)));
    }
    case ButtonGlyphIcon::KeyHome:
        return Union(Line(p, {0.28F, 0.28F}, {0.70F, 0.28F}, 0.035F), Arrow(p, {0.71F, 0.73F}, {0.33F, 0.38F}, 0.035F));
    case ButtonGlyphIcon::KeyEnd:
        return Union(Line(p, {0.30F, 0.72F}, {0.72F, 0.72F}, 0.035F), Arrow(p, {0.29F, 0.27F}, {0.67F, 0.62F}, 0.035F));
    case ButtonGlyphIcon::KeyInsert:
        // The insertion point: a caret onto a line.
        return Union(Line(p, {0.27F, 0.64F}, {0.73F, 0.64F}, 0.035F),
                     Union(Line(p, {0.38F, 0.36F}, {0.5F, 0.54F}, 0.035F), Line(p, {0.5F, 0.54F}, {0.62F, 0.36F}, 0.035F)));
    default:
        break;
    }
    return 1.0F;
}

bool IsKeyIcon(ButtonGlyphIcon icon) noexcept {
    return static_cast<int>(icon) >= static_cast<int>(ButtonGlyphIcon::KeySpace) &&
           icon != ButtonGlyphIcon::Count;
}

// A keycap: a rounded box (a button is round).
Shape Keycap(float silhouette, float mark) noexcept { return {silhouette, Cut(silhouette + kEdge, mark)}; }

Shape IconShape(ButtonGlyphIcon icon, Point p, float margin) noexcept {
    const float radius = 0.5F - margin;
    if (IsKeyIcon(icon))
        return Keycap(RoundedBox(p, kCentre, radius, radius, 0.15F), KeyMark(icon, p));
    const float disc = Circle(p, kCentre, radius);
    return {disc, Cut(Circle(p, kCentre, radius - kEdge), DiscMark(icon, p))};
}

// A frame in its 2 x 1 cell.
Shape FrameShape(ButtonGlyphFrame frame, Point p, float margin) noexcept {
    const float radius = 0.5F - margin;
    const float pill = Segment(p, {0.5F, 0.5F}, {1.5F, 0.5F}) - radius;
    switch (frame) {
    case ButtonGlyphFrame::Button:
        return {pill, pill + kEdge};
    case ButtonGlyphFrame::Key:
        return Keycap(RoundedBox(p, {1.0F, 0.5F}, 1.0F - margin, 0.5F - margin, 0.15F), 1.0F);
    case ButtonGlyphFrame::Stick: {
        const float body = pill + kEdge;
        // A stick's ring: a black circle inside the disc.
        return {pill, Cut(body, Outline(body + 0.095F, 0.026F))};
    }
    case ButtonGlyphFrame::Count:
        break;
    }
    return {};
}

ButtonGlyphAtlas BuildAtlas(std::uint32_t cellSize) {
    const int cell = static_cast<int>(cellSize);
    const float pixel = 1.0F / static_cast<float>(cell);
    const float margin = 1.5F * pixel;
    ButtonGlyphAtlas atlas;
    atlas.Cell = static_cast<std::uint16_t>(cell);
    atlas.Width = static_cast<std::uint16_t>(kIconsPerRow * cell);
    atlas.Height = static_cast<std::uint16_t>((kIconRows + 1) * cell);
    atlas.Rgba8.assign(static_cast<std::size_t>(atlas.Width) * atlas.Height * 4U, 0U);
    const auto coverage = [cell](float distance) {
        return std::clamp(0.5F - distance * static_cast<float>(cell), 0.0F, 1.0F);
    };
    const auto paint = [&](int left, int top, int width, const std::function<Shape(Point)>& shape) {
        for (int y = 0; y < cell; ++y) {
            for (int x = 0; x < width; ++x) {
                const Point p{(static_cast<float>(x) + 0.5F) * pixel, (static_cast<float>(y) + 0.5F) * pixel};
                const auto value = shape(p);
                const float alpha = coverage(value.Silhouette);
                // The body lies inside the silhouette: its share of the
                // covered texel is the texel's lightness.
                const float light = alpha > 0.0F ? std::min(1.0F, coverage(value.Body) / alpha) : 0.0F;
                const auto texel =
                    (static_cast<std::size_t>(top + y) * atlas.Width + static_cast<std::size_t>(left + x)) * 4U;
                const auto level = static_cast<std::uint8_t>(std::lround(light * 255.0F));
                atlas.Rgba8[texel] = level;
                atlas.Rgba8[texel + 1U] = level;
                atlas.Rgba8[texel + 2U] = level;
                atlas.Rgba8[texel + 3U] = static_cast<std::uint8_t>(std::lround(alpha * 255.0F));
            }
        }
    };
    for (int index = 0; index < static_cast<int>(ButtonGlyphIcon::Count); ++index) {
        const auto icon = static_cast<ButtonGlyphIcon>(index);
        paint((index % kIconsPerRow) * cell, (index / kIconsPerRow) * cell, cell,
              [icon, margin](Point p) { return IconShape(icon, p, margin); });
    }
    for (int index = 0; index < static_cast<int>(ButtonGlyphFrame::Count); ++index) {
        const auto frame = static_cast<ButtonGlyphFrame>(index);
        paint(index * 2 * cell, kIconRows * cell, 2 * cell,
              [frame, margin](Point p) { return FrameShape(frame, p, margin); });
    }
    return atlas;
}

bool SingleCharacter(std::string_view label) noexcept {
    if (label.size() <= 1U) return true;
    const auto lead = static_cast<unsigned char>(label[0]);
    return lead >= 0xC0U && label.size() <= (lead >= 0xF0U ? 4U : lead >= 0xE0U ? 3U : 2U);
}

float LabelSize(const ButtonGlyph& glyph, float height) noexcept {
    const bool letter = SingleCharacter(glyph.Label);
    switch (glyph.Frame) {
    case ButtonGlyphFrame::Button: return (letter ? kButtonLetter : kButtonWord) * height;
    case ButtonGlyphFrame::Key: return (letter ? kKeyLetter : kKeyWord) * height;
    case ButtonGlyphFrame::Stick: return kStickLabel * height;
    case ButtonGlyphFrame::Count: break;
    }
    return 0.0F;
}

float LabelPadding(ButtonGlyphFrame frame) noexcept {
    switch (frame) {
    case ButtonGlyphFrame::Button: return 0.42F;
    case ButtonGlyphFrame::Key: return 0.48F;
    case ButtonGlyphFrame::Stick: return 0.62F;
    case ButtonGlyphFrame::Count: break;
    }
    return 0.0F;
}

std::uint32_t NearestCell(std::uint32_t cell) noexcept {
    for (const auto size : kCells)
        if (cell <= size) return size;
    return kCells.back();
}

void Quad(std::vector<UiPrimitive>& output, std::uint32_t cell, float x, float y, float width, float height, float u,
          float v, float uWidth, float vHeight, const UiColor& color, std::uint32_t layer) {
    if (width <= 0.0F || height <= 0.0F) return;
    const auto& atlas = ButtonGlyphAtlasForCell(cell);
    const float atlasWidth = static_cast<float>(atlas.Width);
    const float atlasHeight = static_cast<float>(atlas.Height);
    UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.texture.semantic_name = ButtonGlyphTextureName(atlas.Cell);
    primitive.destination = {x, y, width, height};
    primitive.uv = {u / atlasWidth, v / atlasHeight, uWidth / atlasWidth, vHeight / atlasHeight};
    primitive.color = color;
    primitive.layer = layer;
    primitive.visible = color.alpha > 0.0F;
    output.push_back(std::move(primitive));
}

const char* ShoulderLabel(PromptSet set, bool right) noexcept {
    switch (set) {
    case PromptSet::Xbox: return right ? "RB" : "LB";
    case PromptSet::PlayStation:
    case PromptSet::SteamDeck: return right ? "R1" : "L1";
    default: return right ? "R" : "L";
    }
}

const char* TriggerLabel(PromptSet set, bool right) noexcept {
    switch (set) {
    case PromptSet::Xbox: return right ? "RT" : "LT";
    case PromptSet::Nintendo: return right ? "ZR" : "ZL";
    default: return right ? "R2" : "L2";
    }
}

const char* StickLabel(PromptSet set, bool right) noexcept {
    switch (set) {
    case PromptSet::Xbox:
    case PromptSet::Nintendo: return right ? "RS" : "LS";
    default: return right ? "R3" : "L3";
    }
}

} // namespace

ButtonGlyph GamepadButtonGlyph(PromptSet set, GamepadButton button) {
    const auto button_ = [](const char* label) { return ButtonGlyph::Framed(ButtonGlyphFrame::Button, label); };
    const auto face = [&](std::size_t index) {
        constexpr std::array<ButtonGlyphIcon, 4> kPlayStation{ButtonGlyphIcon::PsCross, ButtonGlyphIcon::PsCircle,
                                                              ButtonGlyphIcon::PsSquare, ButtonGlyphIcon::PsTriangle};
        constexpr std::array<const char*, 4> kLetters{"A", "B", "X", "Y"};
        if (set == PromptSet::PlayStation) return ButtonGlyph::Symbol(kPlayStation[index]);
        return button_(kLetters[index]);
    };
    switch (button) {
    case GamepadButton::A: return face(0);
    case GamepadButton::B: return face(1);
    case GamepadButton::X: return face(2);
    case GamepadButton::Y: return face(3);
    case GamepadButton::LeftShoulder: return button_(ShoulderLabel(set, false));
    case GamepadButton::RightShoulder: return button_(ShoulderLabel(set, true));
    case GamepadButton::LeftTrigger: return button_(TriggerLabel(set, false));
    case GamepadButton::RightTrigger: return button_(TriggerLabel(set, true));
    case GamepadButton::LeftStick: return ButtonGlyph::Framed(ButtonGlyphFrame::Stick, StickLabel(set, false));
    case GamepadButton::RightStick: return ButtonGlyph::Framed(ButtonGlyphFrame::Stick, StickLabel(set, true));
    case GamepadButton::DpadUp: return ButtonGlyph::Symbol(ButtonGlyphIcon::DpadUp);
    case GamepadButton::DpadDown: return ButtonGlyph::Symbol(ButtonGlyphIcon::DpadDown);
    case GamepadButton::DpadLeft: return ButtonGlyph::Symbol(ButtonGlyphIcon::DpadLeft);
    case GamepadButton::DpadRight: return ButtonGlyph::Symbol(ButtonGlyphIcon::DpadRight);
    case GamepadButton::Back:
        switch (set) {
        case PromptSet::Xbox:
        case PromptSet::SteamDeck: return button_("View");
        case PromptSet::PlayStation: return button_("Create");
        case PromptSet::Nintendo: return button_("\xE2\x88\x92"); // minus sign
        default: return button_("Select");
        }
    case GamepadButton::Start:
        switch (set) {
        case PromptSet::Xbox:
        case PromptSet::SteamDeck: return button_("Menu");
        case PromptSet::PlayStation: return button_("Options");
        case PromptSet::Nintendo: return button_("+");
        default: return button_("Start");
        }
    case GamepadButton::Guide: return ButtonGlyph::Symbol(ButtonGlyphIcon::Home);
    case GamepadButton::None: break;
    }
    return button_("?");
}

std::optional<ButtonGlyphIcon> KeyboardKeySymbol(KeyboardKey key) noexcept {
    switch (key) {
    case KeyboardKey::Space: return ButtonGlyphIcon::KeySpace;
    case KeyboardKey::Shift:
    case KeyboardKey::RightShift: return ButtonGlyphIcon::KeyShift;
    case KeyboardKey::Enter: return ButtonGlyphIcon::KeyEnter;
    case KeyboardKey::Backspace: return ButtonGlyphIcon::KeyBackspace;
    case KeyboardKey::Tab: return ButtonGlyphIcon::KeyTab;
    case KeyboardKey::ArrowUp: return ButtonGlyphIcon::KeyUp;
    case KeyboardKey::ArrowDown: return ButtonGlyphIcon::KeyDown;
    case KeyboardKey::ArrowLeft: return ButtonGlyphIcon::KeyLeft;
    case KeyboardKey::ArrowRight: return ButtonGlyphIcon::KeyRight;
    default: return std::nullopt;
    }
}

std::string KeyboardKeyLabel(KeyboardKey key) {
    switch (key) {
    case KeyboardKey::Escape: return "Esc";
    case KeyboardKey::Minus: return "-";
    case KeyboardKey::Plus: return "=";
    case KeyboardKey::Control: return "Ctrl";
    case KeyboardKey::Alt: return "Alt";
    case KeyboardKey::Comma: return ",";
    case KeyboardKey::Period: return ".";
    case KeyboardKey::Slash: return "/";
    case KeyboardKey::Numpad8: return "Num8";
    case KeyboardKey::Numpad4: return "Num4";
    case KeyboardKey::Numpad6: return "Num6";
    case KeyboardKey::Numpad2: return "Num2";
    case KeyboardKey::None: return {};
    default: break;
    }
    // The letters and digits: the input library's one-character names ("q",
    // "1"), as printed on the key.
    const char* name = ThreeDsRecomp::Input::KeyboardKeyName(key);
    std::string label = name != nullptr ? name : "";
    if (label.size() != 1U) return "?";
    if (label[0] >= 'a' && label[0] <= 'z') label[0] = static_cast<char>(label[0] - 'a' + 'A');
    return label;
}

ButtonGlyph KeyboardKeyGlyph(KeyboardKey key) {
    if (const auto symbol = KeyboardKeySymbol(key)) return ButtonGlyph::Symbol(*symbol);
    return ButtonGlyph::Framed(ButtonGlyphFrame::Key, KeyboardKeyLabel(key));
}

ButtonGlyph MouseButtonGlyph(MouseButton button) {
    switch (button) {
    case MouseButton::Right: return ButtonGlyph::Symbol(ButtonGlyphIcon::MouseRight);
    case MouseButton::Middle: return ButtonGlyph::Symbol(ButtonGlyphIcon::MouseMiddle);
    case MouseButton::Back: return ButtonGlyph::Symbol(ButtonGlyphIcon::MouseBack);
    case MouseButton::Forward: return ButtonGlyph::Symbol(ButtonGlyphIcon::MouseForward);
    default: return ButtonGlyph::Symbol(ButtonGlyphIcon::MouseLeft);
    }
}

std::optional<ButtonGlyph> ResolveButtonGlyph(PromptSet set, const ThreeDsRecomp::Input::HostBinding& binding) {
    switch (set) {
    case PromptSet::Touch: return std::nullopt;
    case PromptSet::Keyboard:
        if (binding.KeyboardPrimary != KeyboardKey::None) return KeyboardKeyGlyph(binding.KeyboardPrimary);
        if (binding.KeyboardSecondary != KeyboardKey::None) return KeyboardKeyGlyph(binding.KeyboardSecondary);
        if (binding.Mouse != MouseButton::None) return MouseButtonGlyph(binding.Mouse);
        return std::nullopt;
    default:
        if (binding.Gamepad == GamepadButton::None) return std::nullopt;
        return GamepadButtonGlyph(set, binding.Gamepad);
    }
}

float ButtonGlyphWidth(const ButtonGlyph& glyph, float height) {
    if (!glyph.Labelled) return height;
    const float labelWidth = MeasureText(FontStyle::Heading, LabelSize(glyph, height), glyph.Label);
    return std::max(height, labelWidth + LabelPadding(glyph.Frame) * height);
}

std::uint32_t ButtonGlyphCellForPixels(float pixelHeight) noexcept {
    for (const auto size : kCells)
        if (pixelHeight <= static_cast<float>(size)) return size;
    return kCells.back();
}

void AppendButtonGlyphIcon(std::vector<UiPrimitive>& output, ButtonGlyphIcon icon, float left, float top, float size,
                           const UiColor& color, std::uint32_t layer, std::uint32_t cell) {
    if (icon == ButtonGlyphIcon::Count) return;
    const float texels = static_cast<float>(NearestCell(cell));
    const int index = static_cast<int>(icon);
    Quad(output, cell, left, top, size, size, static_cast<float>(index % kIconsPerRow) * texels,
         static_cast<float>(index / kIconsPerRow) * texels, texels, texels, color, layer);
}

void AppendButtonGlyphFrame(std::vector<UiPrimitive>& output, ButtonGlyphFrame frame, float left, float top,
                            float width, float height, const UiColor& color, std::uint32_t layer, std::uint32_t cell) {
    if (frame == ButtonGlyphFrame::Count) return;
    const float texels = static_cast<float>(NearestCell(cell));
    width = std::max(width, height);
    const float u = static_cast<float>(static_cast<int>(frame)) * 2.0F * texels;
    const float v = static_cast<float>(kIconRows) * texels;
    const float cap = height * 0.5F;
    // The caps (half the height each) and the straight middle stretched
    // between them.
    Quad(output, cell, left, top, cap, height, u, v, texels * 0.5F, texels, color, layer);
    Quad(output, cell, left + cap, top, width - height, height, u + texels * 0.875F, v, texels * 0.25F, texels, color,
         layer);
    Quad(output, cell, left + width - cap, top, cap, height, u + texels * 1.5F, v, texels * 0.5F, texels, color,
         layer);
}

void AppendButtonGlyph(std::vector<UiPrimitive>& output, const ButtonGlyph& glyph, float centerX, float centerY,
                       float height, float alpha, std::uint32_t layer, std::uint32_t cell,
                       const ButtonGlyphColors& colors) {
    if (!(height > 0.0F) || !(alpha > 0.0F)) return;
    UiColor body = colors.Body;
    body.alpha *= alpha;
    const float top = centerY - height * 0.5F;
    if (!glyph.Labelled) {
        AppendButtonGlyphIcon(output, glyph.Icon, centerX - height * 0.5F, top, height, body, layer, cell);
        return;
    }
    if (glyph.Frame == ButtonGlyphFrame::Count) return;
    const float width = ButtonGlyphWidth(glyph, height);
    AppendButtonGlyphFrame(output, glyph.Frame, centerX - width * 0.5F, top, width, height, body, layer, cell);
    UiColor ink = colors.Ink;
    ink.alpha *= alpha;
    const float labelSize = LabelSize(glyph, height);
    const TextStyle style{FontStyle::Heading, labelSize, ink, TextAlign::Center, false, layer};
    // Capitals centred on the glyph's centre (cap height ~0.69 em).
    std::vector<UiPrimitive> label;
    (void)AppendText(label, style, centerX, centerY + 0.345F * labelSize, glyph.Label);
    for (auto& primitive : label) {
        primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
        output.push_back(std::move(primitive));
    }
}

std::string ButtonGlyphTextureName(std::uint32_t cell) {
    return std::string(kButtonGlyphTexturePrefix) + std::to_string(NearestCell(cell));
}

std::optional<std::uint32_t> ButtonGlyphCellFromTextureName(std::string_view name) noexcept {
    if (!name.starts_with(kButtonGlyphTexturePrefix)) return std::nullopt;
    const auto suffix = name.substr(kButtonGlyphTexturePrefix.size());
    for (const auto size : kCells)
        if (suffix == std::to_string(size)) return size;
    return std::nullopt;
}

const ButtonGlyphAtlas& ButtonGlyphAtlasForCell(std::uint32_t cell) {
    static const std::array<ButtonGlyphAtlas, kCells.size()> atlases{BuildAtlas(kCells[0]), BuildAtlas(kCells[1]),
                                                                     BuildAtlas(kCells[2])};
    const auto size = NearestCell(cell);
    for (std::size_t index = 0; index < kCells.size(); ++index)
        if (kCells[index] == size) return atlases[index];
    return atlases.back();
}

} // namespace Oot3dNativeGame::Menu
