#include "oot3d_top_screen_mod_profile.h"

#include "oot3d_ui/ui_contract_types.h"
#include "three_ds_input.h"
#include "three_ds_touch_skin.h"

#include <algorithm>
#include <cmath>

namespace Oot3dNativeGame {
namespace {

using ThreeDsRecomp::Input::Button;
using ThreeDsRecomp::Input::ButtonMask;
using ThreeDsRecomp::Input::TouchAnchor;
using ThreeDsRecomp::Input::TouchSkinShape;

constexpr float kBadgeSeconds = 0.12F;
// Visibility rises quickly (an input answers at once) and falls gently.
constexpr float kShowSeconds = 0.15F;
constexpr float kHideSeconds = 0.4F;
constexpr double kIdleSeconds = 8.0;
// A restricting situation (dialogue, choice, cutscene, title demo) applies
// once it has held this long.
constexpr double kSituationSettleSeconds = 0.3;
constexpr float kIdleVisibility = 0.5F;
// The item controls' level in a native menu (the page itself takes touches
// there), and an empty slot's share.
constexpr float kMenuItemVisibility = 0.6F;
constexpr float kMenuFaceVisibility = 0.25F;
constexpr float kEmptySlotVisibility = 0.55F;
// A pill in a square anchor: as wide as the anchor, this much less tall.
constexpr float kPillAspect = 1.8F;
// Every skin element stays below native icons, counts and action text (12, 13).
constexpr std::uint32_t kShadowLayer = 7U;
constexpr std::uint32_t kFillLayer = 8U;
constexpr std::uint32_t kLitLayer = 9U;
constexpr std::uint32_t kOutlineLayer = 10U;
constexpr std::uint32_t kKnobLayer = 11U;
constexpr std::uint32_t kLetterShadowLayer = 10U;
constexpr std::uint32_t kLetterLayer = 11U;

struct Colors {
  oot3d::ui::UiColor Shadow;
  oot3d::ui::UiColor Fill;
  oot3d::ui::UiColor Gloss;
  oot3d::ui::UiColor Lit;
  oot3d::ui::UiColor Outline;
  oot3d::ui::UiColor Letter;
  oot3d::ui::UiColor LetterShadow;
};

// Muted HUD accents over a common charcoal base. Empty item controls keep
// the navigation controls' neutral rim as well as their lower visibility.
Colors ResolveColors(TouchAnchor control, float opacity, float visibility, float badge) noexcept {
  const float k = std::clamp(opacity, 0.0F, 1.0F) * std::clamp(visibility, 0.0F, 1.0F);
  const float letter = std::min(1.0F, 1.8F * k);
  oot3d::ui::UiColor rim{0.78F, 0.80F, 0.72F, std::min(1.0F, 1.5F * k)};
  auto accent = rim;
  float tint = std::clamp(badge, 0.0F, 1.0F);
  switch (control) {
  case TouchAnchor::A:
    accent = {0.36F, 0.63F, 0.92F, rim.alpha};
    tint = 1.0F;
    break;
  case TouchAnchor::B: accent = {0.42F, 0.76F, 0.40F, rim.alpha}; break;
  case TouchAnchor::X:
  case TouchAnchor::Y: accent = {0.88F, 0.73F, 0.38F, rim.alpha}; break;
  default: break;
  }
  rim.red += (accent.red - rim.red) * tint;
  rim.green += (accent.green - rim.green) * tint;
  rim.blue += (accent.blue - rim.blue) * tint;
  return {{0.0F, 0.0F, 0.0F, 0.32F * k},  {0.035F, 0.045F, 0.050F, 0.80F * k},
          {0.96F, 0.95F, 0.88F, 0.07F * k},  {0.94F, 0.94F, 0.84F, 0.5F * k},
          rim, {0.96F, 0.95F, 0.88F, letter},
          {0.0F, 0.0F, 0.0F, 0.6F * letter}};
}

oot3d::ui::UiPrimitive Shape(TouchSkinShape shape, oot3d::ui::UiRect destination, oot3d::ui::UiRect uv,
                             oot3d::ui::UiColor color, std::uint32_t layer) {
  oot3d::ui::UiPrimitive primitive;
  primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
  primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
  primitive.source_quad = static_cast<std::uint32_t>(shape);
  primitive.texture.semantic_name = ThreeDsRecomp::Input::TouchSkinSemanticName(shape);
  primitive.destination = destination;
  primitive.uv = uv;
  primitive.color = color;
  primitive.layer = layer;
  primitive.visible = color.alpha > 0.0F;
  primitive.texture_alpha_only = true;
  return primitive;
}

constexpr oot3d::ui::UiRect kWholeTexture{0.0F, 0.0F, 1.0F, 1.0F};

oot3d::ui::UiRect Square(float centerX, float centerY, float size) noexcept {
  return {centerX - size * 0.5F, centerY - size * 0.5F, size, size};
}

// A rectangle grown by `scale` about its centre and moved down by `dropY`.
oot3d::ui::UiRect Shadowed(const oot3d::ui::UiRect &rect, float scale, float dropY) noexcept {
  const float width = rect.width * scale;
  const float height = rect.height * scale;
  return {rect.x - (width - rect.width) * 0.5F, rect.y - (height - rect.height) * 0.5F + dropY, width, height};
}

// A square shape turned by quarter turns clockwise (0..3).
oot3d::ui::UiPrimitive Turned(TouchSkinShape shape, oot3d::ui::UiRect destination, int quarterTurns,
                              oot3d::ui::UiColor color, std::uint32_t layer) {
  auto primitive = Shape(shape, destination, kWholeTexture, color, layer);
  std::array<oot3d::ui::UiVertex, 4> corners{};
  const std::array<std::pair<float, float>, 4> positions{{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}}};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    float x = positions[index].first - 0.5F;
    float y = positions[index].second - 0.5F;
    // A point turned clockwise on screen samples the texture turned back.
    for (int turn = 0; turn < quarterTurns; ++turn) {
      const float previousX = x;
      x = y;
      y = -previousX;
    }
    corners[index] = {destination.x + positions[index].first * destination.width,
                      destination.y + positions[index].second * destination.height, x + 0.5F, y + 0.5F};
  }
  primitive.corners = corners;
  return primitive;
}

// Preserve rounded corners and stroke widths while stretching the middle.
// Pills use half-height caps; square tab textures use quarter-height caps.
void AppendCappedShape(TouchSkinShape shape, oot3d::ui::UiRect rect, oot3d::ui::UiColor color, std::uint32_t layer,
                       std::vector<oot3d::ui::UiPrimitive> &output, float capHeight = 0.5F) {
  const float cap = std::min(rect.height * capHeight, rect.width * 0.5F);
  output.push_back(Shape(shape, {rect.x, rect.y, cap, rect.height}, {0.0F, 0.0F, 0.25F, 1.0F}, color, layer));
  if (rect.width > 2.0F * cap)
    output.push_back(Shape(shape, {rect.x + cap, rect.y, rect.width - 2.0F * cap, rect.height},
                           {0.25F, 0.0F, 0.5F, 1.0F}, color, layer));
  output.push_back(Shape(shape, {rect.x + rect.width - cap, rect.y, cap, rect.height}, {0.75F, 0.0F, 0.25F, 1.0F},
                         color, layer));
}

float Ease(float value) noexcept {
  const float t = std::clamp(value, 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

float Approach(float value, float target, float up, float down) noexcept {
  return value < target ? std::min(target, value + up) : std::max(target, value - down);
}

TouchSkinShape LetterOf(TouchAnchor anchor) noexcept {
  switch (anchor) {
  case TouchAnchor::A: return TouchSkinShape::GlyphA;
  case TouchAnchor::B: return TouchSkinShape::GlyphB;
  case TouchAnchor::X: return TouchSkinShape::GlyphX;
  case TouchAnchor::Y: return TouchSkinShape::GlyphY;
  case TouchAnchor::Zl: return TouchSkinShape::GlyphZl;
  case TouchAnchor::Zr: return TouchSkinShape::GlyphZr;
  case TouchAnchor::L: return TouchSkinShape::GlyphL;
  case TouchAnchor::R: return TouchSkinShape::GlyphR;
  case TouchAnchor::Start: return TouchSkinShape::GlyphStart;
  case TouchAnchor::Select: return TouchSkinShape::GlyphSelect;
  case TouchAnchor::Home: return TouchSkinShape::GlyphHome;
  default: return TouchSkinShape::Count;
  }
}

std::uint32_t ButtonOf(TouchAnchor anchor) noexcept {
  switch (anchor) {
  case TouchAnchor::A: return ButtonMask(Button::A);
  case TouchAnchor::B: return ButtonMask(Button::B);
  case TouchAnchor::X: return ButtonMask(Button::X);
  case TouchAnchor::Y: return ButtonMask(Button::Y);
  case TouchAnchor::Zl: return ButtonMask(Button::Zl);
  case TouchAnchor::Zr: return ButtonMask(Button::Zr);
  case TouchAnchor::L: return ButtonMask(Button::L);
  case TouchAnchor::R: return ButtonMask(Button::R);
  case TouchAnchor::Start: return ButtonMask(Button::Start);
  case TouchAnchor::Select: return ButtonMask(Button::Select);
  default: return 0U;
  }
}

bool IsPillControl(TouchAnchor anchor) noexcept {
  switch (anchor) {
  case TouchAnchor::L:
  case TouchAnchor::R:
  case TouchAnchor::Zl:
  case TouchAnchor::Zr:
  case TouchAnchor::Start:
  case TouchAnchor::Select:
  case TouchAnchor::Home: return true;
  default: return false;
  }
}

bool IsSystemControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::Start || anchor == TouchAnchor::Select || anchor == TouchAnchor::Home;
}

bool IsHeaderControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::Select || anchor == TouchAnchor::Home;
}

bool IsShoulderControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::L || anchor == TouchAnchor::R || anchor == TouchAnchor::Zl || anchor == TouchAnchor::Zr;
}

// The buttons whose HUD slot shows an item when one is assigned.
bool IsSlotControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::X || anchor == TouchAnchor::Y || anchor == TouchAnchor::Zl ||
         anchor == TouchAnchor::Zr || anchor == TouchAnchor::B;
}

bool IsFaceItemControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::X || anchor == TouchAnchor::Y || anchor == TouchAnchor::B;
}

// Only the controls that do something in the situation show.
float TargetVisibility(TouchAnchor control, TopScreenTouchSituation situation, const TopScreenTouchContext &context,
                       bool idle) noexcept {
  float target = 1.0F;
  switch (situation) {
  case TopScreenTouchSituation::Menu:
    target = control == TouchAnchor::Dpad ? kMenuItemVisibility : 1.0F;
    if (IsFaceItemControl(control)) target = kMenuFaceVisibility;
    break;
  case TopScreenTouchSituation::TitleDemo:
    target = control == TouchAnchor::A || control == TouchAnchor::Start ? 1.0F : 0.0F;
    break;
  case TopScreenTouchSituation::Dialogue:
    target = control == TouchAnchor::A ? 1.0F : 0.0F;
    break;
  case TopScreenTouchSituation::Choice:
    target = control == TouchAnchor::A || control == TouchAnchor::CirclePad || control == TouchAnchor::Dpad ? 1.0F
                                                                                                            : 0.0F;
    break;
  case TopScreenTouchSituation::Cutscene:
    target = 0.0F;
    break;
  case TopScreenTouchSituation::Gameplay:
    // As much as the game shows its HUD buttons.
    target = context.HudExpected ? std::clamp(context.HudAlpha, 0.0F, 1.0F) : 1.0F;
    if (target > 0.5F && IsSlotControl(control) && !context.Content[static_cast<std::size_t>(control)])
      target *= kEmptySlotVisibility;
    break;
  }
  return idle ? target * kIdleVisibility : target;
}

bool Restricts(TopScreenTouchSituation situation) noexcept {
  return situation == TopScreenTouchSituation::TitleDemo || situation == TopScreenTouchSituation::Dialogue ||
         situation == TopScreenTouchSituation::Choice || situation == TopScreenTouchSituation::Cutscene;
}

void AppendDisc(const oot3d::ui::UiRect &square, const Colors &colors, bool held, std::uint32_t fillLayer,
                std::vector<oot3d::ui::UiPrimitive> &output, TouchSkinShape rim = TouchSkinShape::Ring) {
  output.push_back(Shape(TouchSkinShape::DiscSoft, Shadowed(square, 1.16F, square.height * 0.04F), kWholeTexture,
                         colors.Shadow, kShadowLayer));
  output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Fill, fillLayer));
  if (held) output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Lit, kLitLayer));
  output.push_back(Shape(TouchSkinShape::Gloss, square, kWholeTexture, colors.Gloss, kLitLayer));
  output.push_back(Shape(rim, square, kWholeTexture, colors.Outline, kOutlineLayer));
}

void AppendTab(const oot3d::ui::UiRect &rect, const Colors &colors, bool held,
               std::vector<oot3d::ui::UiPrimitive> &output) {
  AppendCappedShape(TouchSkinShape::RoundedBoxSoft, Shadowed(rect, 1.08F, rect.height * 0.03F), colors.Shadow,
                    kShadowLayer, output, 0.25F);
  AppendCappedShape(TouchSkinShape::RoundedBox, rect, colors.Fill, kFillLayer, output, 0.25F);
  if (held) AppendCappedShape(TouchSkinShape::RoundedBox, rect, colors.Lit, kLitLayer, output, 0.25F);
  output.push_back(Shape(TouchSkinShape::Gloss, rect, kWholeTexture, colors.Gloss, kLitLayer));
  AppendCappedShape(TouchSkinShape::RoundedBoxRing, rect, colors.Outline, kOutlineLayer, output, 0.25F);
}

void AppendLetter(TouchSkinShape letter, const oot3d::ui::UiRect &square, const Colors &colors,
                  std::vector<oot3d::ui::UiPrimitive> &output) {
  if (letter == TouchSkinShape::Count) return;
  auto shadow = square;
  shadow.x += square.width * 0.03F;
  shadow.y += square.height * 0.045F;
  output.push_back(Shape(letter, shadow, kWholeTexture, colors.LetterShadow, kLetterShadowLayer));
  output.push_back(Shape(letter, square, kWholeTexture, colors.Letter, kLetterLayer));
}

void AppendDpad(const oot3d::ui::UiRect &rect, const Colors &colors, std::uint32_t held, float badge,
                std::vector<oot3d::ui::UiPrimitive> &output) {
  const float size = std::min(rect.width, rect.height);
  const float centerX = rect.x + rect.width * 0.5F;
  const float centerY = rect.y + rect.height * 0.5F;
  const auto square = Square(centerX, centerY, size);
  output.push_back(Shape(TouchSkinShape::CrossSoft, Shadowed(square, 1.1F, size * 0.03F), kWholeTexture,
                         colors.Shadow, kShadowLayer));
  output.push_back(Shape(TouchSkinShape::Cross, square, kWholeTexture, colors.Fill, kFillLayer));
  // Up, right, down, left: quarter turns clockwise.
  constexpr std::array<Button, 4> kArms{Button::DpadUp, Button::DpadRight, Button::DpadDown, Button::DpadLeft};
  constexpr std::array<std::pair<float, float>, 4> kDirections{{{0.0F, -1.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {-1.0F, 0.0F}}};
  for (int arm = 0; arm < 4; ++arm) {
    if ((held & ButtonMask(kArms[static_cast<std::size_t>(arm)])) != 0U)
      output.push_back(Turned(TouchSkinShape::CrossArm, square, arm, colors.Lit, kLitLayer));
  }
  output.push_back(Shape(TouchSkinShape::CrossRing, square, kWholeTexture, colors.Outline, kOutlineLayer));
  // Native arm icons identify the functions without an arrow underneath them.
  if (badge > 0.0F) return;
  for (int arm = 0; arm < 4; ++arm) {
    const auto [dx, dy] = kDirections[static_cast<std::size_t>(arm)];
    output.push_back(Turned(TouchSkinShape::Arrow, Square(centerX + dx * size * 0.36F, centerY + dy * size * 0.36F,
                                                          size * 0.15F),
                            arm, colors.Letter, kLetterLayer));
  }
}

void AppendCirclePad(const oot3d::ui::UiRect &rect, const Colors &colors, float padX, float padY,
                     std::vector<oot3d::ui::UiPrimitive> &output) {
  const float size = std::min(rect.width, rect.height);
  const float centerX = rect.x + rect.width * 0.5F;
  const float centerY = rect.y + rect.height * 0.5F;
  AppendDisc(Square(centerX, centerY, size), colors, false, kFillLayer, output, TouchSkinShape::RingThin);
  const float knob = size * 0.44F;
  const float x = std::clamp(padX, -1.0F, 1.0F);
  const float y = std::clamp(padY, -1.0F, 1.0F);
  const float travel = (size - knob) * 0.5F * 0.85F;
  const bool moved = x != 0.0F || y != 0.0F;
  const auto knobSquare = Square(centerX + x * travel, centerY - y * travel, knob);
  auto knobFill = colors.Lit;
  knobFill.alpha *= moved ? 1.5F : 0.8F;
  output.push_back(Shape(TouchSkinShape::DiscSoft, Shadowed(knobSquare, 1.2F, knob * 0.06F), kWholeTexture,
                         colors.Shadow, kKnobLayer));
  output.push_back(Shape(TouchSkinShape::Disc, knobSquare, kWholeTexture, knobFill, kKnobLayer));
  output.push_back(Shape(TouchSkinShape::Gloss, knobSquare, kWholeTexture, colors.Gloss, kKnobLayer));
  output.push_back(Shape(TouchSkinShape::Ring, knobSquare, kWholeTexture, colors.Outline, kKnobLayer));
}

} // namespace

TopScreenTouchControlShape ResolveTopScreenTouchControlShape(TouchAnchor control,
                                                             const ThreeDsRecomp::Input::TouchAnchorRect &anchor,
                                                             float canvasWidth) noexcept {
  TopScreenTouchControlShape shape;
  const float width = (anchor.Right - anchor.Left) * canvasWidth;
  const float height = (anchor.Bottom - anchor.Top) * 240.0F;
  const float centerX = anchor.CenterX() * canvasWidth;
  const float centerY = anchor.CenterY() * 240.0F;
  if (IsHeaderControl(control) || IsShoulderControl(control)) {
    const bool header = IsHeaderControl(control);
    const bool primaryShoulder = control == TouchAnchor::L || control == TouchAnchor::R;
    const float bodyWidth = header ? width * 0.72F : width;
    float bodyHeight = std::min(height, width);
    float top = centerY - bodyHeight * 0.5F;
    if (header) {
      bodyHeight = height * 0.42F;
      top = anchor.Top * 240.0F + height * 0.08F;
    } else if (primaryShoulder) {
      bodyHeight = std::min(height, width * 0.72F);
      top = anchor.Top * 240.0F;
    }
    shape.Rect = {centerX - bodyWidth * 0.5F, top, bodyWidth, bodyHeight};
    shape.ContentHalf = std::min(bodyWidth, bodyHeight) * 0.625F;
    shape.ContentX = centerX;
    shape.ContentY = top + bodyHeight * 0.5F;
    return shape;
  }
  if (IsPillControl(control) || width >= height * 1.2F) {
    const float aspect = IsSystemControl(control) ? 1.4F : kPillAspect;
    const float pillHeight = std::min(height, width / aspect);
    shape.Pill = true;
    shape.Rect = {centerX - width * 0.5F, centerY - pillHeight * 0.5F, width, pillHeight};
    shape.ContentHalf = pillHeight * 0.625F;
    shape.ContentX = centerX;
    shape.ContentY = centerY;
    return shape;
  }
  const float size = std::min(width, height);
  shape.Rect = Square(centerX, centerY, size);
  // The extractor's 0.9 fill factor makes a native 28/32 item occupy about
  // 98% of a face disc. Native content may extend beyond the skin's edge.
  shape.ContentHalf = size * (control == TouchAnchor::Dpad ? 0.62F : 0.625F);
  shape.ContentX = centerX;
  shape.ContentY = centerY;
  return shape;
}

void AdvanceTopScreenTouchSkin(TopScreenTouchSkinState &state, const TopScreenTouchContext &context,
                               double seconds) noexcept {
  const float elapsed = seconds > 0.0 ? static_cast<float>(seconds) : 0.0F;
  state.IdleSeconds = context.Active ? 0.0 : state.IdleSeconds + elapsed;
  const bool idle = state.IdleSeconds > kIdleSeconds;
  // A situation that opens up the controls applies at once; a restricting
  // one once it has held (a door is not a cutscene to hide the controls for).
  if (context.Situation != state.PendingSituation) {
    state.PendingSituation = context.Situation;
    state.PendingSeconds = 0.0;
  } else {
    state.PendingSeconds += elapsed;
  }
  if (!state.Started || !Restricts(context.Situation) || state.PendingSeconds >= kSituationSettleSeconds)
    state.Situation = context.Situation;
  for (std::size_t index = 0; index < state.Badge.size(); ++index) {
    const float badgeStep = elapsed > 0.0F ? elapsed / kBadgeSeconds : 1.0F;
    // A new icon must never share a frame with a skin letter. Retain the
    // short release delay so a disappearing icon does not flash its letter.
    state.Badge[index] = context.Content[index] ? 1.0F : Approach(state.Badge[index], 0.0F, badgeStep, badgeStep);
    const float target = TargetVisibility(static_cast<TouchAnchor>(index), state.Situation, context, idle);
    state.Visibility[index] =
        state.Started ? Approach(state.Visibility[index], target, elapsed / kShowSeconds, elapsed / kHideSeconds)
                      : target;
  }
  state.Started = true;
}

void AppendTopScreenTouchSkin(const TopScreenTouchSkinInput &input, const TopScreenTouchSkinState &state,
                              std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!(input.CanvasWidth > 0.0F)) return;
  for (std::size_t index = 0; index < ThreeDsRecomp::Input::kTouchAnchorCount; ++index) {
    const auto &anchor = input.Anchors[index];
    if (!anchor.Present) continue;
    const auto control = static_cast<TouchAnchor>(index);
    const float opacity = input.Opacity * (IsHeaderControl(control) ? 0.60F : 1.0F);
    auto colors = ResolveColors(control, opacity, state.Started ? state.Visibility[index] : 1.0F,
                                Ease(state.Badge[index]));
    if (state.Situation == TopScreenTouchSituation::Menu && IsFaceItemControl(control)) {
      // Native equipment slots remain visible through the overlapping controls.
      colors.Fill.alpha = 0.0F;
      colors.Shadow.alpha = 0.0F;
      colors.Gloss.alpha = 0.0F;
    }
    if (state.Situation == TopScreenTouchSituation::Menu && IsSystemControl(control)) {
      colors.Fill.alpha *= 0.375F;
      colors.Shadow.alpha *= 0.5F;
      colors.Gloss.alpha *= 0.5F;
    }
    const oot3d::ui::UiRect rect{anchor.Left * input.CanvasWidth, anchor.Top * 240.0F,
                                 (anchor.Right - anchor.Left) * input.CanvasWidth,
                                 (anchor.Bottom - anchor.Top) * 240.0F};
    if (control == TouchAnchor::Dpad) {
      AppendDpad(rect, colors, input.HeldButtons, state.Badge[index], output);
      continue;
    }
    if (control == TouchAnchor::CirclePad) {
      AppendCirclePad(rect, colors, input.CirclePadX, input.CirclePadY, output);
      continue;
    }
    const bool held = (input.HeldButtons & ButtonOf(control)) != 0U;
    // In a text, A is the text's "next": a right-pointing triangle.
    const bool next = control == TouchAnchor::A && (state.Situation == TopScreenTouchSituation::Dialogue ||
                                                    state.Situation == TopScreenTouchSituation::Choice);
    const bool content = !next && state.Badge[index] > 0.0F;
    const auto shape = ResolveTopScreenTouchControlShape(control, anchor, input.CanvasWidth);
    const auto &body = shape.Rect;
    const float centerY = body.y + body.height * 0.5F;
    const auto letter = next ? TouchSkinShape::GlyphStart : LetterOf(control);
    if (state.Situation == TopScreenTouchSituation::Menu &&
        IsHeaderControl(control)) {
      // Keep menu headings clear. The compact icon remains inside the full
      // touch target; START's default position is in the right bar instead.
      const float size = std::min(rect.width, rect.height) * 0.28F;
      const float y = rect.y + rect.height * 0.04F + size * 0.5F;
      if (!content) AppendLetter(letter, Square(body.x + body.width * 0.5F, y, size), colors, output);
      continue;
    }
    if (IsHeaderControl(control) || IsShoulderControl(control)) {
      AppendTab(body, colors, held, output);
      if (!content) {
        const float size = std::min(body.width, body.height) * (IsHeaderControl(control) ? 0.65F : 0.60F);
        AppendLetter(letter, Square(body.x + body.width * 0.5F, centerY, size), colors, output);
      }
      continue;
    }
    if (shape.Pill) {
      AppendCappedShape(TouchSkinShape::PillSoft, Shadowed(body, 1.1F, body.height * 0.06F), colors.Shadow, kShadowLayer,
                 output);
      AppendCappedShape(TouchSkinShape::Pill, body, colors.Fill, kFillLayer, output);
      if (held) AppendCappedShape(TouchSkinShape::Pill, body, colors.Lit, kLitLayer, output);
      output.push_back(Shape(TouchSkinShape::Gloss, body, kWholeTexture, colors.Gloss, kLitLayer));
      AppendCappedShape(TouchSkinShape::PillRing, body, colors.Outline, kOutlineLayer, output);
      if (!content)
        AppendLetter(letter, Square(body.x + body.width * 0.5F, centerY, body.height * 0.7F), colors, output);
      continue;
    }
    const float centerX = body.x + body.width * 0.5F;
    AppendDisc(body, colors, held, kFillLayer, output);
    if (!content) AppendLetter(letter, Square(centerX, centerY, body.width * 0.44F), colors, output);
  }
}

} // namespace Oot3dNativeGame
