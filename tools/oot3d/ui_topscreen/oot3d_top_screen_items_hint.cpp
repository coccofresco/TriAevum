#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_dpad_picker.h"

#include "oot3d_ui/ui_backend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint16_t kHintDelayFrames = 90U;
constexpr std::uint16_t kHintFadeFrames = 12U;
constexpr std::uint16_t kMaximumStableFrames =
    kHintDelayFrames + kHintFadeFrames;
constexpr std::uint32_t kPulsePeriod = 90U;
constexpr float kCursorMovementEpsilon = 0.5F;

// custom_ui01 rects 0x0060A6F8 (SELECT) and 0x0060A708 (?), 512x256.
constexpr TopScreenItemsHintQuad kSelectTemplate{
    true, 0.0F, 0.0F, 0.0F, 0.0F, 277.0F, 222.0F, 64.0F, 16.0F, 0.0F};
constexpr TopScreenItemsHintQuad kQuestionTemplate{
    true, 0.0F, 0.0F, 0.0F, 0.0F, 344.0F, 222.0F, 16.0F, 16.0F, 0.0F};
// 0x005E2A30 / 0x005E2A34: native X clamps of the hint pair.
constexpr float kMinimumNativeX = -80.0F;
constexpr float kMaximumNativeX = 318.0F;

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
  const float minimumX = state.Cursor.ScreenOffsetX + kMinimumNativeX;
  const float maximumX = state.Cursor.ScreenOffsetX + kMaximumNativeX;
  select.X = minimumX;
  const float centeredX = state.Cursor.CenterX - totalWidth * 0.5F;
  if (centeredX >= minimumX) {
    select.X = centeredX;
  }
  if (select.X + totalWidth > maximumX) {
    select.X = maximumX - totalWidth;
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
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Visible || uiAtlas.semantic_name.empty()) {
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
    primitive.owner_address = 0x005E0E6CU;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = uiAtlas;
    primitive.destination = {quad.X, quad.Y, quad.Width, quad.Height};
    primitive.uv = {quad.AtlasX / 512.0F, quad.AtlasY / 256.0F,
                    quad.AtlasWidth / 512.0F, quad.AtlasHeight / 256.0F};
    primitive.color.alpha = quad.Alpha;
    primitive.layer = 15U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenItemsShortcutPresentation(
    const TopScreenItemsShortcutState &state, const TopScreenItemsShortcutDrawState &draw,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!draw.Active) return 0U;
  const auto start = output.size();
  for (unsigned row = 0; row < 4; ++row) {
    const auto action = ResolveTopScreenItemsShortcutAction(state.Order, row);
    if ((action == TopScreenDpadAction::Ocarina && draw.Ocarina == 0xFF) ||
        (action == TopScreenDpadAction::AllBootsToggle && !draw.BootsOwned)) continue;
    const bool moving = static_cast<int>(row) == state.MovingRow;
    const bool swapping = state.SwapFrame >= 0 && state.SwapFrame < 7;
    const bool target = swapping && static_cast<int>(row) == state.SwapTarget;
    const bool enlarged = moving || target ||
        (static_cast<int>(row) == state.SelectedRow && !state.SelectionSettled && !swapping);
    const float size = enlarged ? 52.0F : 42.0F;
    const float x = enlarged ? 42.0F : 47.0F;
    const float rowY = swapping && moving ? static_cast<float>(state.MovingY) :
                       target ? static_cast<float>(state.TargetY) : row * 48.0F + 7.0F;
    const float y = rowY - (enlarged ? 5.0F : 0.0F);
    const float brightness = draw.Dimmed || !TopScreenItemsShortcutAvailable(action, draw) ? 0.3F : 1.0F;
    const auto rowStart = output.size();
    const auto append = [&](const oot3d::ui::UiTextureIdentity &texture, unsigned source,
                            float dx, float dy, float dw, float dh,
                            float ax, float ay, float aw, float ah, float atlasHeight) {
      if (texture.semantic_name.empty()) return;
      oot3d::ui::UiPrimitive primitive;
      primitive.subsystem = oot3d::ui::UiSubsystem::Items;
      primitive.role = oot3d::ui::UiPrimitiveRole::PauseItem;
      primitive.source_quad = source;
      primitive.texture = texture;
      primitive.destination = {dx, dy, dw, dh};
      primitive.uv = {ax / 512.0F, ay / atlasHeight, aw / 512.0F, ah / atlasHeight};
      // 0x005CBC18 writes brightness to RGB and leaves alpha at one.
      primitive.color = {brightness, brightness, brightness, 1.0F};
      primitive.layer = source >= 4U ? 15U : 14U;
      output.push_back(std::move(primitive));
    };
    if (action == TopScreenDpadAction::View) {
      // Rect 4 at 0x0060A5A8, inset by one texel: 47x30 from (452,73).
      const float height = size * 30.0F / 47.0F;
      append(uiAtlas, row, x + 1.0F, y + (size - height) * 0.5F, size, height,
             452.0F, 73.0F, 47.0F, 30.0F, 256.0F);
    } else if (action == TopScreenDpadAction::MinimapToggle) {
      append(itemIcons, row, x, y, size, size, 420.5F, 380.5F, 41.0F, 41.0F, 512.0F);
    } else {
      const auto item = action == TopScreenDpadAction::Ocarina ? draw.Ocarina : draw.Boots;
      append(itemIcons, row, x, y, size, size,
             (item % 12U) * 42.0F, (item / 12U) * 42.0F, 42.0F, 42.0F, 512.0F);
      if (action == TopScreenDpadAction::AllBootsToggle)
        append(uiAtlas, row + 4U, x, y, size, size, 320.0F, 3.0F, 40.0F, 40.0F, 256.0F);
    }
    // 0x005E3264 draws a white, pulsing copy of the grabbed icon after
    // the cursor. The cycle badge stays at its ordinary brightness.
    if (moving && output.size() > rowStart) {
      auto pulse = output[rowStart];
      const auto phase = state.PulseFrame % 32U;
      const float alpha = state.PulseFrame == 0U ? 1.0F :
                          static_cast<float>(phase < 16U ? phase : 32U - phase) / 16.0F;
      pulse.color = {1.0F, 1.0F, 1.0F, alpha};
      pulse.layer = 16U;
      pulse.source_quad = row + 8U;
      output.push_back(std::move(pulse));
    }
  }
  return output.size() - start;
}

namespace {
struct PickerGlyph {
  char Character;
  float X, Y, Width, Height, OffsetY, OffsetX, Advance;
};
// Used glyph metrics from 0x00609F1C; custom_ui01 stays 512x256.
constexpr std::array<PickerGlyph, 27> kPickerGlyphs{{
    {'B',350,170,21,25,3,-1,19}, {'a',143,208,20,20,8,-2,16},
    {'c',25,209,19,20,8,-2,16}, {'k',149,180,20,25,3,-2,15},
    {'C',77,124,22,26,2,-2,19}, {'l',374,197,11,25,3,-2,9},
    {'e',326,143,20,20,8,-2,16}, {'r',220,124,16,20,8,-2,12},
    {' ',0,0,0,0,0,0,10}, {'A',462,139,24,25,3,-3,19},
    {'(',349,108,15,31,1,-2,11}, {'H',102,152,22,25,3,-1,21},
    {'o',296,199,21,20,8,-2,17}, {'d',486,167,20,25,3,-2,17},
    {')',367,108,15,31,1,-3,11}, {'V',300,143,23,25,3,-3,19},
    {'i',441,196,11,25,3,-2,8}, {'w',224,204,26,20,8,-3,21},
    {'M',165,124,25,25,3,-1,24}, {'Z',250,176,21,25,3,-2,18},
    {'m',195,204,26,20,8,-2,23}, {'I',388,197,11,25,3,-1,10},
    {'n',79,153,20,20,8,-2,17}, {'O',435,140,24,25,3,-2,21},
    {'u',97,208,20,20,8,-2,17}, {'t',320,46,16,24,4,-3,11},
    {'P',296,171,21,25,3,-1,18}}};
const PickerGlyph *PickerCharacter(char character) {
  for (const auto &glyph : kPickerGlyphs) if (glyph.Character == character) return &glyph;
  return nullptr;
}

// 0x0060A2D4 rects for the button glyphs of 0x005E7B44: 0x0E X, 0x0F Y,
// 0x15 B and 0x16 D-pad, all at rect scale 1. The D-pad record keeps the
// old (434,219) crop; its 0x005E5578 path remaps it through 0x005E517C.
struct LegendButton { char Character; float X, Y, Width, Height; };
constexpr std::array<LegendButton, 4> kLegendButtons{{
    {'\x0E', 116, 89, 32, 32}, {'\x0F', 151, 89, 32, 32},
    {'\x15', 81, 89, 32, 32}, {'\x16', 385, 108, 29, 29}}};
const LegendButton *LegendButtonCharacter(char character) {
  for (const auto &button : kLegendButtons) if (button.Character == character) return &button;
  return nullptr;
}
// 0x005E6D20 / 0x005E7B44 text units: 0.74 * 0.5714 per scale unit.
constexpr float kLegendUnit = 0.422836F;
float LegendButtonHeight(float scale) { return scale * kLegendUnit * 19.0F * 1.55F; }

float MeasureLegend(std::string_view text, float scale) {
  float width = 0;
  for (const char c : text) {
    if (const auto *button = LegendButtonCharacter(c))
      width += LegendButtonHeight(scale) * button->Width / button->Height + 2;
    else if (const auto *glyph = PickerCharacter(c))
      width += glyph->Advance * scale * kLegendUnit;
  }
  return width;
}

// 0x005E7B44(text, x, y, scale) in native page coordinates. Button glyphs sit
// on the line y + 2 + 15.5 units; font glyphs hang from y + 2.
void AppendLegend(std::string_view text, float x, float y, float scale, float alpha,
                  float canvasOffsetX, std::uint32_t owner, unsigned layer,
                  const oot3d::ui::UiTextureIdentity &uiAtlas,
                  std::vector<oot3d::ui::UiPrimitive> &output) {
  const float unit = scale * kLegendUnit;
  const auto append = [&](float px, float py, float w, float h,
                          float ax, float ay, float aw, float ah) {
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::Items;
    p.role = oot3d::ui::UiPrimitiveRole::PauseText;
    p.owner_address = owner;
    p.source_quad = static_cast<std::uint32_t>(output.size());
    p.texture = uiAtlas;
    p.destination = {px + canvasOffsetX, py, w, h};
    p.uv = {ax / 512.0F, ay / 256.0F, aw / 512.0F, ah / 256.0F};
    p.color.alpha = alpha;
    p.layer = layer;
    output.push_back(std::move(p));
  };
  for (const char c : text) {
    if (const auto *button = LegendButtonCharacter(c)) {
      const float h = LegendButtonHeight(scale), w = h * button->Width / button->Height;
      append(x + 1, y + 2 + 15.5F * unit - h / 2, w, h,
             button->X, button->Y, button->Width, button->Height);
      x += w + 2;
    } else if (const auto *g = PickerCharacter(c)) {
      if (g->Width > 0)
        append(x + g->OffsetX * unit, y + 2 + g->OffsetY * unit, g->Width * unit,
               g->Height * unit, g->X, g->Y, g->Width, g->Height);
      x += g->Advance * unit;
    }
  }
}

// 0x005E70A4: the custom_ui01 nine-slice panel, corners 12x6 or half the size.
void AppendLegendPanel(float x, float y, float width, float height, float alpha,
                       float canvasOffsetX, std::uint32_t owner, unsigned layer,
                       const oot3d::ui::UiTextureIdentity &uiAtlas,
                       std::vector<oot3d::ui::UiPrimitive> &output) {
  const float cornerW = width < 24 ? width * 0.5F : 12, cornerH = height < 12 ? height * 0.5F : 6;
  constexpr std::array<float, 3> atlasX{375, 387, 491}, atlasY{38, 44, 64};
  constexpr std::array<float, 3> atlasW{12, 1, 12}, atlasH{6, 1, 6};
  const std::array<float, 3> xs{x, x + cornerW, x + width - cornerW};
  const std::array<float, 3> ys{y, y + cornerH, y + height - cornerH};
  const std::array<float, 3> ws{cornerW, width - 2 * cornerW, cornerW};
  const std::array<float, 3> hs{cornerH, height - 2 * cornerH, cornerH};
  for (unsigned row = 0; row < 3; ++row)
    for (unsigned column = 0; column < 3; ++column) {
      if (ws[column] <= 0 || hs[row] <= 0) continue;
      oot3d::ui::UiPrimitive p;
      p.subsystem = oot3d::ui::UiSubsystem::Items;
      p.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
      p.owner_address = owner;
      p.source_quad = static_cast<std::uint32_t>(output.size());
      p.texture = uiAtlas;
      p.destination = {xs[column] + canvasOffsetX, ys[row], ws[column], hs[row]};
      p.uv = {atlasX[column] / 512.0F, atlasY[row] / 256.0F,
              atlasW[column] / 512.0F, atlasH[row] / 256.0F};
      p.color.alpha = alpha;
      p.layer = layer;
      output.push_back(std::move(p));
    }
}
} // namespace

std::size_t AppendTopScreenGearViewModelLegendPresentation(
    const TopScreenItemsHintCursorBounds &cursor, std::uint32_t fade, std::uint32_t pulse,
    const oot3d::ui::UiTextureIdentity &uiAtlas, std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!cursor.Active || fade == 0U || uiAtlas.semantic_name.empty() ||
      !std::isfinite(cursor.CenterX) || !std::isfinite(cursor.MaximumY))
    return 0U;
  // 0x005E2BA8: the hint pulse, faded in over the first 12 updates.
  const std::uint32_t phase = pulse % 90U;
  float t = static_cast<float>(phase) / 45.0F;
  if (phase > 44U) t = 2.0F - t;
  float alpha = 0.73F + t * 0.27F;
  if (fade <= 11U) alpha *= static_cast<float>(fade) / 12.0F;
  // 0x005E9A58, projection 0.8: [X] at 0.85 and "View Model" at 0.62,
  // centred under the cursor and kept within native X 2..318.
  const float centerX = cursor.CenterX - cursor.ScreenOffsetX, y = cursor.MaximumY + 1.0F;
  const float button = MeasureLegend("\x0E", 0.85F), space = MeasureLegend(" ", 0.62F);
  const float total = button + space + MeasureLegend("View Model", 0.62F);
  float x = std::max(centerX - total * 0.5F, 2.0F);
  if (x + total > 318.0F) x = 318.0F - total;
  const auto start = output.size();
  AppendLegend("\x0E", x, y + 0.4291358F, 0.85F, alpha, cursor.ScreenOffsetX, 0x005E9A58U,
               15U, uiAtlas, output);
  AppendLegend("View Model", x + button + space, y + 1.9365458F, 0.62F, alpha,
               cursor.ScreenOffsetX, 0x005E9A58U, 15U, uiAtlas, output);
  return output.size() - start;
}

std::size_t AppendTopScreenViewModelControlsPresentation(
    const oot3d::ui::UiTextureIdentity &uiAtlas, std::vector<oot3d::ui::UiPrimitive> &output) {
  if (uiAtlas.semantic_name.empty()) return 0U;
  // 0x005E9C3C, projection 0.8 (native X + 40): one line at 0.75, narrowed
  // to 360, on a 24-high panel at Y 210 that is 32 wider, centred on X 160.
  constexpr std::string_view text = "\x0E Zoom In   \x0F Zoom Out   \x16 Pan   \x15 Back";
  float scale = 0.75F;
  float width = MeasureLegend(text, scale);
  if (width > 360.0F) {
    scale *= 360.0F / width;
    width = MeasureLegend(text, scale);
  }
  const auto start = output.size();
  AppendLegendPanel(160.0F - (width + 32.0F) * 0.5F, 210.0F, width + 32.0F, 24.0F, 1.0F,
                    40.0F, 0x005E9C3CU, 31U, uiAtlas, output);
  // 0x005E7EC0 centres the line on X 160 with its glyphs on Y 222.
  AppendLegend(text, 160.0F - width * 0.5F, 222.0F - (2.0F + 15.5F * scale * kLegendUnit),
               scale, 1.0F, 40.0F, 0x005E9C3CU, 36U, uiAtlas, output);
  return output.size() - start;
}


TopScreenDpadPickerHoldCursor BuildTopScreenDpadPickerHoldCursor(
    float centerX, float centerY, float progress) noexcept {
  TopScreenDpadPickerHoldCursor result;
  if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(progress)) return result;
  result.CenterX = centerX;
  result.CenterY = centerY;
  // 0x005D1708/0C changes the native template to RGB REPLACE and alpha
  // scale 2. 0x003146E4 packs that scale into TEV stage 0, register 0xC4.
  result.TextureAlphaScale = 2.0F;
  progress = std::clamp(progress, 0.0F, 1.0F);
  const auto append = [&](unsigned first, unsigned last, oot3d::ui::UiColor color) {
    constexpr float radians = 6.283185307179586F / 65536.0F;
    const float s0 = std::sin(first * radians), c0 = std::cos(first * radians);
    const float s1 = std::sin(last * radians), c1 = std::cos(last * radians);
    constexpr float outer = 12.133333F, inner = 6.466667F;
    // 0x005CCAE4/0x005CCB1C rebind slot 5 with linear/clamp sampling.
    // 0x005CBC68 writes GPU V=(256-y)/256; our top-left V is y/256.
    result.Quads[result.Count++] = {{{
        {centerX + s0 * outer, centerY - c0 * outer, 423.0F / 512, 189.5F / 256},
        {centerX + s1 * outer, centerY - c1 * outer, 423.0F / 512, 192.5F / 256},
        {centerX + s0 * inner, centerY - c0 * inner, 428.0F / 512, 189.5F / 256},
        {centerX + s1 * inner, centerY - c1 * inner, 428.0F / 512, 192.5F / 256}}}, color};
  };
  for (unsigned i = 0; i < 24; ++i) append(i * 65536U / 24U, (i + 1U) * 65536U / 24U, {0,0,0,1});
  const float filled = progress * 24;
  const unsigned complete = static_cast<unsigned>(filled);
  for (unsigned i = 0; i < complete; ++i)
    append(i * 65536U / 24U, (i + 1U) * 65536U / 24U, {1,0.86F,0.12F,1});
  if (complete < 24 && filled > complete) {
    const auto first = complete * 65536U / 24U;
    append(first, first + static_cast<unsigned>((filled - complete) * 2730.0F), {1,0.86F,0.12F,1});
  }
  return result;
}

TopScreenDpadPickerHoldCursor BuildTopScreenItemsShortcutHoldCursor(
    const TopScreenItemsShortcutState &state,
    const TopScreenItemsHintCursorBounds &bounds) noexcept {
  if (state.SelectedRow < 0 || state.SelectedRow >= 4 || state.MovingRow >= 0 ||
      state.SwapFrame >= 0 || state.HoldFrames < 2 || state.HoldFrames >= 15 ||
      !bounds.Active || !std::isfinite(bounds.CenterX) ||
      !std::isfinite(bounds.Width) || bounds.Width <= 0 ||
      !std::isfinite(bounds.MinimumY) || !std::isfinite(bounds.MaximumY) ||
      bounds.MaximumY <= bounds.MinimumY) return {};
  // Shortcuts use the left/grid projection (0.58): 0x005E3468..0x005E34A4
  // places the ring just beyond the cursor right edge, with a 2px overlap.
  return BuildTopScreenDpadPickerHoldCursor(
      bounds.CenterX + bounds.Width * 0.5F + 12.133333F - 2.0F,
      (bounds.MinimumY + bounds.MaximumY) * 0.5F, state.HoldFrames / 15.0F);
}

std::size_t AppendTopScreenItemsShortcutHoldPresentation(
    const TopScreenDpadPickerHoldCursor &holdCursor,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (holdCursor.Count == 0 || uiAtlas.semantic_name.empty()) return 0U;
  // 0x005CCA90 defaults to 0x005EB43C: custom_ui01 A rect at 0x0060A728.
  oot3d::ui::UiPrimitive glyph;
  glyph.subsystem = oot3d::ui::UiSubsystem::Items;
  glyph.role = oot3d::ui::UiPrimitiveRole::PauseCursor;
  glyph.owner_address = 0x005E34A4U;
  glyph.texture = uiAtlas;
  glyph.destination = {holdCursor.CenterX - 6.5F, holdCursor.CenterY - 6.5F, 13, 13};
  glyph.uv = {46.0F / 512, 89.0F / 256, 32.0F / 512, 32.0F / 256};
  glyph.layer = 38U;
  output.push_back(std::move(glyph));
  return 1U;
}

std::size_t AppendTopScreenDpadPickerPresentation(
    const TopScreenDpadPickerState &state, const TopScreenDpadPickerDrawState &draw,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output, TopScreenDpadPickerHoldCursor *holdCursor) {
  if (holdCursor != nullptr) *holdCursor = {};
  using Phase = TopScreenDpadPickerPhase;
  using Role = oot3d::ui::UiPrimitiveRole;
  if (!state.Active() || uiAtlas.semantic_name.empty()) return 0U;
  const float alpha = state.Phase == Phase::Opening ? state.Frame / 8.0F :
                      state.Phase == Phase::Closing ? 1.0F - state.Frame / 6.0F : 1.0F;
  if (alpha <= 0.0F) return 0U;
  const auto start = output.size();
  const auto append = [&](const oot3d::ui::UiTextureIdentity &texture, unsigned source,
                          Role role, float x, float y, float w, float h,
                          float ax, float ay, float aw, float ah, float atlasHeight,
                          float opacity, unsigned layer) {
    if (texture.semantic_name.empty() || opacity <= 0.0F) return;
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::Items;
    p.role = role;
    p.owner_address = 0x005E9348U;
    p.source_quad = source;
    p.texture = texture;
    p.destination = {x, y, w, h};
    p.uv = {ax / 512.0F, ay / atlasHeight, aw / 512.0F, ah / atlasHeight};
    p.color.alpha = opacity;
    p.layer = layer;
    output.push_back(std::move(p));
  };
  // 0x005E9348 uses projection 0.8: native X + 40 on the 400x240 canvas.
  // The full-canvas dimmer and nine-slice frame are custom_ui01 quads.
  append(uiAtlas, 0, Role::PauseBackground, 0, 0, 400, 240,
         402, 197, 16, 16, 256, alpha, 30);
  const float scale = 0.85F + alpha * 0.15F;
  const float width = 156.0F * scale, height = 172.0F * scale;
  const float left = 200.0F - width / 2.0F, top = 120.0F - height / 2.0F;
  constexpr std::array<float, 3> atlasX{375, 387, 491}, atlasY{38, 44, 64};
  constexpr std::array<float, 3> atlasW{12, 1, 12}, atlasH{6, 1, 6};
  const std::array<float, 3> xs{left, left + 12, left + width - 12};
  const std::array<float, 3> ys{top, top + 6, top + height - 6};
  const std::array<float, 3> ws{12, width - 24, 12}, hs{6, height - 12, 6};
  for (unsigned y = 0; y < 3; ++y)
    for (unsigned x = 0; x < 3; ++x)
      append(uiAtlas, 1U + y * 3U + x, Role::PauseBackground,
             xs[x], ys[y], ws[x], hs[y], atlasX[x], atlasY[y], atlasW[x], atlasH[y],
             256, alpha, 31);
  const float centerY = 120.0F - 10.0F * scale;
  const float cross = 58.0F * scale;
  // 0x005D1DCC -> 0x005E517C remaps the old D-pad crop to (385,108,29,29).
  append(uiAtlas, 10, Role::PauseCursor, 200 - cross / 2, centerY - cross / 2, cross, cross,
         385, 108, 29, 29, 256, alpha, 32);
  const auto icon = [&](TopScreenDpadAction action, unsigned source, float x, float y,
                        float size, float opacity, bool overlay, bool center) {
    if (action == TopScreenDpadAction::None) return;
    const auto role = overlay ? Role::PauseCursor : Role::PauseItem;
    const unsigned layer = overlay ? 35U : 33U;
    if (action == TopScreenDpadAction::View) {
      const float h = size * 30.0F / 47.0F;
      append(uiAtlas, source, role, x + (center ? (overlay ? 0.5F : 1.5F) : 0.0F),
             y + (size - h) / 2.0F - (center ? 1.0F : 0.0F), size, h,
             452, 73, 47, 30, 256, opacity, layer);
    } else if (action == TopScreenDpadAction::MinimapToggle) {
      append(itemIcons, source, role, x, y, size, size,
             420.5F, 380.5F, 41, 41, 512, opacity, layer);
    } else {
      const unsigned index = static_cast<unsigned>(action);
      if (index >= draw.Items.size() || draw.Items[index] >= 0x90U) return;
      const auto item = draw.Items[index];
      append(itemIcons, source, role, x, y, size, size,
             (item % 12U) * 42.0F + 0.5F, (item / 12U) * 42.0F + 0.5F,
             41, 41, 512, opacity, layer);
      if (!overlay && (action == TopScreenDpadAction::AllBootsToggle ||
                       action == TopScreenDpadAction::SwordToggle ||
                       action == TopScreenDpadAction::TunicToggle ||
                       action == TopScreenDpadAction::ShieldToggle))
        append(uiAtlas, source + 100, role, x, y, size, size,
               320, 3, 40, 40, 256, opacity, 34);
    }
  };
  constexpr std::array<float, 4> offsetX{0.9F, 0.9F, -50, 50};
  constexpr std::array<float, 4> offsetY{-50, 50, 0, 0};
  const bool confirmation = state.Phase == Phase::Confirming ||
      (state.Phase == Phase::Closing && state.Direction >= 0);
  const float progress = state.Phase == Phase::Confirming ? state.Frame / 12.0F : 1.0F;
  if (alpha >= 0.5F) {
    for (unsigned direction = 0; direction < 4; ++direction) {
      float size = 28.0F * scale;
      if (confirmation && state.Direction == static_cast<int>(direction))
        size *= 1.0F + (1.0F - std::abs(progress * 2.0F - 1.0F)) * 0.25F;
      const float correction = direction < 2 && state.Assignments[direction] == TopScreenDpadAction::View
                                   ? 0.5F : 0.0F;
      icon(state.Assignments[direction], 20U + direction,
           200 + offsetX[direction] * scale - size / 2 + correction,
           centerY + offsetY[direction] * scale - size / 2, size, 1, false, false);
    }
    if (confirmation && state.Direction >= 0 && state.Direction < 4) {
      // 0x005E9808: ease-out motion to the confirmed direction with a fading copy.
      const float t = 1 - (1 - progress) * (1 - progress);
      const float size = (30 - 2 * t) * scale;
      icon(state.Action, 31, 200 + offsetX[state.Direction] * scale * t - size / 2,
           centerY + offsetY[state.Direction] * scale * t - size / 2,
           size, 1 - progress * progress, true, false);
    } else {
      const float size = 30.0F * scale;
      icon(state.Action, 30, 200 - size / 2, centerY - size / 2, size, 1, false, true);
      if (alpha >= 1) {
        const unsigned phase = state.PulseFrame & 31U;
        const float pulse = ((phase & 16U) ? 31U - phase : phase) / 16.0F;
        icon(state.Action, 31, 200 - size / 2, centerY - size / 2,
             size, 0.1F + 0.6F * pulse, true, true);
      }
    }
  }
  // 0x005E962C / 0x005E7B44: [B] Back  [X] Clear All (Hold), and [Y] View
  // Model when 0x005D289C allows it, fitted as one centred line. Each glyph
  // keeps the native atlas bearings and advances.
  constexpr float fontUnit = 0.422836F;
  constexpr float symbolUnit = 0.74F * 0.5714F * 19.0F * 1.55F;
  constexpr std::array<std::string_view, 3> labels{"Back", "Clear All (Hold)", "View Model"};
  constexpr std::array<float, 3> buttonX{81, 116, 151};
  const unsigned rows = draw.ViewModel ? 3U : 2U;
  const auto measure = [&](std::string_view label, float font) {
    float w = 0;
    for (const char c : label) if (const auto *g = PickerCharacter(c)) w += g->Advance * font * fontUnit;
    return w;
  };
  float font = 0.75F;
  const auto legendWidth = [&] {
    float w = rows * (2 + symbolUnit * font * 1.35F) + (10.0F * rows + 20.0F * (rows - 1)) * font * fontUnit;
    for (unsigned i = 0; i < rows; ++i) w += measure(labels[i], font);
    return w;
  };
  if (legendWidth() > width - 24) font *= (width - 24) / legendWidth();
  const float symbol = symbolUnit * font * 1.35F;
  float legendX = 200 - legendWidth() / 2;
  const float legendY = top + height - 22;
  unsigned glyphIndex = 0;
  float clearCenter = 0;
  for (unsigned i = 0; i < rows; ++i) {
    append(uiAtlas, 40U + i, Role::PauseText, legendX + 1, legendY + 8 - symbol / 2,
           symbol, symbol, buttonX[i], 89, 32, 32, 256, alpha, 36);
    if (i == 1) clearCenter = legendX + (symbol + 2) / 2;
    legendX += symbol + 2 + 10 * font * fontUnit;
    const float unit = font * fontUnit;
    const float baseline = legendY + 8 - font * 0.74F * 0.5714F * 15.5F;
    for (const char c : labels[i]) {
      if (const auto *g = PickerCharacter(c)) {
        if (g->Width > 0)
          append(uiAtlas, 50U + glyphIndex++, Role::PauseText,
                 legendX + g->OffsetX * unit, baseline + g->OffsetY * unit,
                 g->Width * unit, g->Height * unit, g->X, g->Y, g->Width, g->Height,
                 256, alpha, 36);
        legendX += g->Advance * unit;
      }
    }
    legendX += 20 * font * fontUnit;
  }
  if (state.Phase == Phase::Choosing && state.ClearHoldFrames > 1) {
    // 0x005E1054 -> 0x005CCA90: a native 24-segment ring, plus a 13px X.
    // The lifecycle exposes its mesh for the generic quad consumer.
    if (holdCursor != nullptr)
      *holdCursor = BuildTopScreenDpadPickerHoldCursor(clearCenter, legendY + 8,
                                                      state.ClearHoldFrames / 45.0F);
    append(uiAtlas, 90, Role::PauseCursor, clearCenter - 6.5F, legendY + 1.5F,
           13, 13, 116, 89, 32, 32, 256, 1, 38);
  }
  return output.size() - start;
}

} // namespace Oot3dNativeGame
