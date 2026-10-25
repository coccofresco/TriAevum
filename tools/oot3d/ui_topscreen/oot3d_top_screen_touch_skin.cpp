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
constexpr float kEmptySlotVisibility = 0.55F;
// A pill in a square anchor: as wide as the anchor, this much less tall.
constexpr float kPillAspect = 1.8F;
// Layers: the base under the HUD content (12, 13), the letters over it.
constexpr std::uint32_t kShadowLayer = 7U;
constexpr std::uint32_t kFillLayer = 8U;
constexpr std::uint32_t kLitLayer = 9U;
constexpr std::uint32_t kOutlineLayer = 10U;
constexpr std::uint32_t kKnobLayer = 11U;
constexpr std::uint32_t kLetterShadowLayer = 14U;
constexpr std::uint32_t kLetterLayer = 15U;

struct Colors {
  oot3d::ui::UiColor Shadow;
  oot3d::ui::UiColor Fill;
  oot3d::ui::UiColor Gloss;
  oot3d::ui::UiColor Lit;
  oot3d::ui::UiColor Outline;
  oot3d::ui::UiColor Letter;
  oot3d::ui::UiColor LetterShadow;
};

// The overlay's opacity times the control's visibility. At the default 50%:
// a fill that keeps bright scenes readable, a clear outline, letters near full.
Colors ResolveColors(float opacity, float visibility) noexcept {
  const float k = std::clamp(opacity, 0.0F, 1.0F) * std::clamp(visibility, 0.0F, 1.0F);
  const float letter = std::min(1.0F, 1.8F * k);
  return {{0.0F, 0.0F, 0.0F, 0.55F * k},  {0.0F, 0.0F, 0.0F, 0.75F * k},
          {1.0F, 1.0F, 1.0F, 0.18F * k},  {1.0F, 1.0F, 1.0F, 0.5F * k},
          {1.0F, 1.0F, 1.0F, std::min(1.0F, 1.5F * k)}, {1.0F, 1.0F, 1.0F, letter},
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

// The pill in three slices: caps half its height wide, the middle stretched.
void AppendPill(TouchSkinShape shape, oot3d::ui::UiRect rect, oot3d::ui::UiColor color, std::uint32_t layer,
                std::vector<oot3d::ui::UiPrimitive> &output) {
  const float cap = std::min(rect.height * 0.5F, rect.width * 0.5F);
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

float Mix(float from, float to, float t) noexcept { return from + (to - from) * t; }

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

// The controls that carry an item and fade with the HUD.
bool IsItemControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::X || anchor == TouchAnchor::Y || anchor == TouchAnchor::Zl ||
         anchor == TouchAnchor::Zr || anchor == TouchAnchor::Dpad;
}

// The buttons whose HUD slot shows an item when one is assigned.
bool IsSlotControl(TouchAnchor anchor) noexcept {
  return anchor == TouchAnchor::X || anchor == TouchAnchor::Y || anchor == TouchAnchor::Zl ||
         anchor == TouchAnchor::Zr || anchor == TouchAnchor::B;
}

// Only the controls that do something in the situation show.
float TargetVisibility(TouchAnchor control, TopScreenTouchSituation situation, const TopScreenTouchContext &context,
                       bool idle) noexcept {
  float target = 1.0F;
  switch (situation) {
  case TopScreenTouchSituation::Menu:
    target = IsItemControl(control) ? kMenuItemVisibility : 1.0F;
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
                std::vector<oot3d::ui::UiPrimitive> &output) {
  output.push_back(Shape(TouchSkinShape::DiscSoft, Shadowed(square, 1.16F, square.height * 0.04F), kWholeTexture,
                         colors.Shadow, kShadowLayer));
  output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Fill, fillLayer));
  if (held) output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Lit, kLitLayer));
  output.push_back(Shape(TouchSkinShape::Gloss, square, kWholeTexture, colors.Gloss, kLitLayer));
  output.push_back(Shape(TouchSkinShape::Ring, square, kWholeTexture, colors.Outline, kOutlineLayer));
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
  // Arrows at the arm tips step back while the HUD's icons sit on the arms.
  auto arrow = colors.Letter;
  arrow.alpha *= 1.0F - 0.75F * Ease(badge);
  for (int arm = 0; arm < 4; ++arm) {
    const auto [dx, dy] = kDirections[static_cast<std::size_t>(arm)];
    output.push_back(Turned(TouchSkinShape::Arrow, Square(centerX + dx * size * 0.36F, centerY + dy * size * 0.36F,
                                                          size * 0.15F),
                            arm, arrow, kLetterLayer));
  }
}

void AppendCirclePad(const oot3d::ui::UiRect &rect, const Colors &colors, float padX, float padY,
                     std::vector<oot3d::ui::UiPrimitive> &output) {
  const float size = std::min(rect.width, rect.height);
  const float centerX = rect.x + rect.width * 0.5F;
  const float centerY = rect.y + rect.height * 0.5F;
  AppendDisc(Square(centerX, centerY, size), colors, false, kFillLayer, output);
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
  if (IsPillControl(control) || width >= height * 1.2F) {
    const float pillHeight = std::min(height, width / kPillAspect);
    shape.Pill = true;
    shape.Rect = {centerX - width * 0.5F, centerY - pillHeight * 0.5F, width, pillHeight};
    shape.ContentHalf = pillHeight * 0.5F;
    shape.ContentX = shape.Rect.x + width - shape.ContentHalf;
    shape.ContentY = centerY;
    return shape;
  }
  const float size = std::min(width, height);
  shape.Rect = Square(centerX, centerY, size);
  shape.ContentHalf = size * 0.5F;
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
    state.Badge[index] = Approach(state.Badge[index], context.Content[index] ? 1.0F : 0.0F, badgeStep, badgeStep);
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
    const auto colors = ResolveColors(input.Opacity, state.Started ? state.Visibility[index] : 1.0F);
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
    const float badge = next ? 0.0F : Ease(state.Badge[index]);
    const auto shape = ResolveTopScreenTouchControlShape(control, anchor, input.CanvasWidth);
    const auto &body = shape.Rect;
    const float centerY = body.y + body.height * 0.5F;
    const auto letter = next ? TouchSkinShape::GlyphStart : LetterOf(control);
    if (shape.Pill) {
      // The letter moves to the left cap while the content takes the right end.
      AppendPill(TouchSkinShape::PillSoft, Shadowed(body, 1.1F, body.height * 0.06F), colors.Shadow, kShadowLayer,
                 output);
      AppendPill(TouchSkinShape::Pill, body, colors.Fill, kFillLayer, output);
      if (held) AppendPill(TouchSkinShape::Pill, body, colors.Lit, kLitLayer, output);
      output.push_back(Shape(TouchSkinShape::Gloss, body, kWholeTexture, colors.Gloss, kLitLayer));
      AppendPill(TouchSkinShape::PillRing, body, colors.Outline, kOutlineLayer, output);
      const float size = Mix(0.7F, 0.46F, badge) * body.height;
      const float x = Mix(body.x + body.width * 0.5F, body.x + body.height * 0.5F, badge);
      AppendLetter(letter, Square(x, centerY, size), colors, output);
      continue;
    }
    const float centerX = body.x + body.width * 0.5F;
    AppendDisc(body, colors, held, kFillLayer, output);
    // The letter centred, or a badge on the top-left edge over content.
    const float size = Mix(0.44F, 0.26F, badge) * body.width;
    const float offset = -0.31F * body.width * badge;
    AppendLetter(letter, Square(centerX + offset, centerY + offset, size), colors, output);
  }
}

} // namespace Oot3dNativeGame
