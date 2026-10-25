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
// Layers: the base under the HUD content (12, 13), the letters over it.
constexpr std::uint32_t kFillLayer = 8U;
constexpr std::uint32_t kOutlineLayer = 9U;
constexpr std::uint32_t kKnobLayer = 10U;
constexpr std::uint32_t kLetterLayer = 15U;

struct Colors {
  oot3d::ui::UiColor Fill;
  oot3d::ui::UiColor Lit;
  oot3d::ui::UiColor Outline;
  oot3d::ui::UiColor Letter;
};

Colors ResolveColors(float opacity) noexcept {
  const float k = std::clamp(opacity, 0.0F, 1.0F);
  // At the overlay's default 50%: a fill that keeps bright scenes readable,
  // a clear outline and letters near full.
  return {{0.0F, 0.0F, 0.0F, 0.75F * k},
          {1.0F, 1.0F, 1.0F, 0.5F * k},
          {1.0F, 1.0F, 1.0F, std::min(1.0F, 1.5F * k)},
          {1.0F, 1.0F, 1.0F, std::min(1.0F, 1.8F * k)}};
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
  const float cap = rect.height * 0.5F;
  output.push_back(Shape(shape, {rect.x, rect.y, cap, rect.height}, {0.0F, 0.0F, 0.25F, 1.0F}, color, layer));
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

void AppendDpad(const oot3d::ui::UiRect &rect, const Colors &colors, std::uint32_t held, float badge,
                std::vector<oot3d::ui::UiPrimitive> &output) {
  const float size = std::min(rect.width, rect.height);
  const float centerX = rect.x + rect.width * 0.5F;
  const float centerY = rect.y + rect.height * 0.5F;
  const auto square = Square(centerX, centerY, size);
  output.push_back(Shape(TouchSkinShape::Cross, square, kWholeTexture, colors.Fill, kFillLayer));
  // Up, right, down, left: quarter turns clockwise.
  constexpr std::array<Button, 4> kArms{Button::DpadUp, Button::DpadRight, Button::DpadDown, Button::DpadLeft};
  constexpr std::array<std::pair<float, float>, 4> kDirections{{{0.0F, -1.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {-1.0F, 0.0F}}};
  for (int arm = 0; arm < 4; ++arm) {
    if ((held & ButtonMask(kArms[static_cast<std::size_t>(arm)])) != 0U)
      output.push_back(Turned(TouchSkinShape::CrossArm, square, arm, colors.Lit, kOutlineLayer));
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
  const auto square = Square(centerX, centerY, size);
  output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Fill, kFillLayer));
  output.push_back(Shape(TouchSkinShape::Ring, square, kWholeTexture, colors.Outline, kOutlineLayer));
  const float knob = size * 0.42F;
  const float x = std::clamp(padX, -1.0F, 1.0F);
  const float y = std::clamp(padY, -1.0F, 1.0F);
  const float travel = (size - knob) * 0.5F * 0.85F;
  const bool moved = x != 0.0F || y != 0.0F;
  const auto knobSquare = Square(centerX + x * travel, centerY - y * travel, knob);
  auto knobFill = colors.Lit;
  knobFill.alpha *= moved ? 1.6F : 0.9F;
  output.push_back(Shape(TouchSkinShape::Disc, knobSquare, kWholeTexture, knobFill, kKnobLayer));
  output.push_back(Shape(TouchSkinShape::Ring, knobSquare, kWholeTexture, colors.Outline, kKnobLayer));
}

} // namespace

void AdvanceTopScreenTouchSkin(TopScreenTouchSkinState &state,
                               const std::array<bool, ThreeDsRecomp::Input::kTouchAnchorCount> &content,
                               double seconds) noexcept {
  const float step = seconds > 0.0 ? static_cast<float>(seconds) / kBadgeSeconds : 1.0F;
  for (std::size_t index = 0; index < state.Badge.size(); ++index) {
    const float target = content[index] ? 1.0F : 0.0F;
    auto &badge = state.Badge[index];
    badge = badge < target ? std::min(target, badge + step) : std::max(target, badge - step);
  }
}

void AppendTopScreenTouchSkin(const TopScreenTouchSkinInput &input, const TopScreenTouchSkinState &state,
                              std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!(input.CanvasWidth > 0.0F)) return;
  const auto colors = ResolveColors(input.Opacity);
  for (std::size_t index = 0; index < ThreeDsRecomp::Input::kTouchAnchorCount; ++index) {
    const auto &anchor = input.Anchors[index];
    if (!anchor.Present) continue;
    const auto control = static_cast<TouchAnchor>(index);
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
    const float badge = Ease(state.Badge[index]);
    const float centerX = rect.x + rect.width * 0.5F;
    const float centerY = rect.y + rect.height * 0.5F;
    const auto letter = LetterOf(control);
    if (rect.width >= rect.height * 1.2F) {
      // A wide control: a pill, its letter moving to the left cap while the
      // content takes the square at the right end.
      AppendPill(TouchSkinShape::Pill, rect, colors.Fill, kFillLayer, output);
      if (held) AppendPill(TouchSkinShape::Pill, rect, colors.Lit, kOutlineLayer, output);
      AppendPill(TouchSkinShape::PillRing, rect, colors.Outline, kOutlineLayer, output);
      const float size = Mix(0.66F, 0.44F, badge) * rect.height;
      const float x = Mix(centerX, rect.x + rect.height * 0.5F, badge);
      output.push_back(Shape(letter, Square(x, centerY, size), kWholeTexture, colors.Letter, kLetterLayer));
      continue;
    }
    const float diameter = std::min(rect.width, rect.height);
    const auto square = Square(centerX, centerY, diameter);
    output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Fill, kFillLayer));
    if (held) output.push_back(Shape(TouchSkinShape::Disc, square, kWholeTexture, colors.Lit, kOutlineLayer));
    output.push_back(Shape(TouchSkinShape::Ring, square, kWholeTexture, colors.Outline, kOutlineLayer));
    // The letter centred, or a badge on the top-left edge over content.
    const float size = Mix(0.44F, 0.26F, badge) * diameter;
    const float offset = -0.31F * diameter * badge;
    output.push_back(
        Shape(letter, Square(centerX + offset, centerY + offset, size), kWholeTexture, colors.Letter, kLetterLayer));
  }
}

} // namespace Oot3dNativeGame
