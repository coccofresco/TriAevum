#include "oot3d_top_screen_items_hint.h"

#include "oot3d_ui/ui_backend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr float kPromotedPauseHorizontalScale = 400.0F / 320.0F;
constexpr std::uint16_t kHintDelayFrames = 90U;
constexpr std::uint16_t kHintFadeFrames = 12U;
constexpr std::uint16_t kMaximumStableFrames =
    kHintDelayFrames + kHintFadeFrames;
constexpr std::uint32_t kPulsePeriod = 90U;
constexpr float kCursorMovementEpsilon = 0.5F;

constexpr TopScreenItemsHintQuad kSelectTemplate{
    true, 0.0F, 0.0F, 0.0F, 0.0F, 434.0F, 410.0F, 64.0F, 16.0F, 0.0F};
constexpr TopScreenItemsHintQuad kQuestionTemplate{
    true, 0.0F, 0.0F, 0.0F, 0.0F, 492.0F, 104.0F, 16.0F, 16.0F, 0.0F};

float AspectHeight(float width, const TopScreenItemsHintQuad &source) noexcept {
  return source.AtlasWidth > 0.0F
             ? width * source.AtlasHeight / source.AtlasWidth
             : source.AtlasHeight;
}

float AspectWidth(float height, const TopScreenItemsHintQuad &source) noexcept {
  return source.AtlasHeight > 0.0F
             ? height * source.AtlasWidth / source.AtlasHeight
             : source.AtlasWidth;
}

float ResolveHintAlpha(const TopScreenItemsHintRuntimeState &state) noexcept {
  const std::uint32_t phase = state.AnimationFrame % kPulsePeriod;
  const float triangle = phase < kPulsePeriod / 2U
                             ? static_cast<float>(phase) / 45.0F
                             : 2.0F - static_cast<float>(phase) / 45.0F;
  float alpha = 0.73F + triangle * 0.27F;
  const std::uint16_t fadeFrame = state.StableFrames - kHintDelayFrames;
  if (fadeFrame < kHintFadeFrames) {
    alpha *=
        static_cast<float>(fadeFrame) / static_cast<float>(kHintFadeFrames);
  }
  return alpha;
}

} // namespace

void AdvanceTopScreenItemsHintState(
    TopScreenItemsHintRuntimeState &state,
    const TopScreenItemsHintCursorBounds &cursor) noexcept {
  ++state.AnimationFrame;
  state.Cursor = cursor;
  if (!cursor.Active || !std::isfinite(cursor.CenterX) ||
      !std::isfinite(cursor.MinimumY) || !std::isfinite(cursor.MaximumY) ||
      !std::isfinite(cursor.Width) || cursor.Width <= 0.0F) {
    state.Cursor.Active = false;
    state.PreviousCenterX = 0.5F;
    state.PreviousMinimumY = 0.5F;
    state.StableFrames = 0U;
    state.Tracking = false;
    return;
  }

  const bool stable = state.Tracking &&
                      std::abs(cursor.CenterX - state.PreviousCenterX) <=
                          kCursorMovementEpsilon &&
                      std::abs(cursor.MinimumY - state.PreviousMinimumY) <=
                          kCursorMovementEpsilon;
  if (stable) {
    state.StableFrames = std::min<std::uint16_t>(
        static_cast<std::uint16_t>(state.StableFrames + 1U),
        kMaximumStableFrames);
  } else {
    state.StableFrames = 0U;
    state.PreviousCenterX = cursor.CenterX;
    state.PreviousMinimumY = cursor.MinimumY;
  }
  state.Tracking = true;
}

TopScreenItemsHintGeometry
BuildTopScreenItemsHintGeometry(const TopScreenItemsHintRuntimeState &state,
                                bool enabled) noexcept {
  TopScreenItemsHintGeometry result;
  if (!enabled || !state.Cursor.Active ||
      state.StableFrames < kHintDelayFrames) {
    return result;
  }

  auto select = kSelectTemplate;
  auto question = kQuestionTemplate;
  select.Width = state.Cursor.Width * 0.8F;
  select.Height = AspectHeight(select.Width, select);
  select.Y = state.Cursor.MaximumY + 4.0F;
  if (select.Y + select.Height > 224.0F) {
    select.Y = state.Cursor.MinimumY - 4.0F - select.Height;
  }
  select.Y -= 4.0F;

  question.Height = select.Height * 1.45F;
  question.Width = AspectWidth(question.Height, question);
  const float totalWidth = select.Width + 3.0F + question.Width;
  select.X = 2.0F;
  const float centeredX = state.Cursor.CenterX - totalWidth * 0.5F;
  if (centeredX >= 2.0F) {
    select.X = centeredX;
  }
  if (select.X + totalWidth > 318.0F) {
    select.X = 318.0F - totalWidth;
  }
  question.X = select.X + select.Width + 3.0F;
  question.Y = select.Y + (select.Height - question.Height) * 0.5F;

  const float alpha = ResolveHintAlpha(state);
  select.Alpha = alpha;
  question.Alpha = alpha;
  result.Visible = true;
  result.Quads = {select, question};
  return result;
}

std::size_t AppendTopScreenItemsHintPresentation(
    const TopScreenItemsHintGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &menuAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Visible || menuAtlas.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0U; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::Items;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
    primitive.owner_address = 0x005D2078U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = menuAtlas;
    primitive.destination = {quad.X * kPromotedPauseHorizontalScale, quad.Y,
                             quad.Width * kPromotedPauseHorizontalScale,
                             quad.Height};
    primitive.uv = {quad.AtlasX / 512.0F, quad.AtlasY / 512.0F,
                    quad.AtlasWidth / 512.0F, quad.AtlasHeight / 512.0F};
    primitive.color.alpha = quad.Alpha;
    primitive.layer = 15U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

} // namespace Oot3dNativeGame
