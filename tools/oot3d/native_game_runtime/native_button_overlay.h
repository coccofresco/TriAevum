#pragma once

// Title-neutral bridge between native UI ownership and the existing controller
// glyph art. Titles supply materialized label bounds, never screen guesses.
#include "oot3d_menu_button_glyphs.h"
#include "oot3d_menu_font.h"
#include "oot3d_native_control_config.h"
#include <algorithm>
#include <cmath>

namespace Oot3dNativeGame {
struct NativeButtonOverlaySlot {
  NativeControlAction Action = NativeControlAction::Count;
  float X = 0, Y = 0, Height = 0;
  oot3d::ui::UiColor Color{1, 1, 1, 1};
  NativeControlAction Modifier = NativeControlAction::Count;
};

inline std::optional<Menu::ButtonGlyph> NativeButtonOverlayGlyph(
    NativeControlAction action, NativePromptSet prompts, const NativeControlConfig& controls) {
  const auto index = static_cast<std::size_t>(action);
  if (!controls.NativeButtonLabels || index >= controls.Bindings.size()) return {};
  auto binding = controls.Bindings[index];
  if (!controls.MouseEnabled) binding.Mouse = NativeMouseButton::None;
  return Menu::ResolveButtonGlyph(prompts, binding);
}

inline void AppendNativeButtonOverlay(std::vector<oot3d::ui::UiPrimitive>& output,
    const std::vector<NativeButtonOverlaySlot>& slots, NativePromptSet prompts,
    const NativeControlConfig& controls, float pixelsPerUnit) {
  for (const auto& slot : slots) {
    const auto glyph = NativeButtonOverlayGlyph(slot.Action, prompts, controls);
    if (!glyph || !std::isfinite(slot.X) || !std::isfinite(slot.Y) ||
        !std::isfinite(slot.Height) || slot.Height <= 0 || slot.Height > 128 ||
        !std::isfinite(slot.Color.alpha) || slot.Color.alpha <= 0 || slot.Color.alpha > 1) continue;
    const auto modifier = NativeButtonOverlayGlyph(slot.Modifier, prompts, controls);
    if (slot.Modifier != NativeControlAction::Count && !modifier) continue;
    Menu::ButtonGlyphColors colors;
    const float light = std::clamp(std::min({slot.Color.red, slot.Color.green, slot.Color.blue}), 0.f, 1.f);
    colors.Body = {light, light, light, 1};
    Menu::AppendButtonGlyph(output, *glyph, slot.X, slot.Y, slot.Height, slot.Color.alpha, 30,
        Menu::ButtonGlyphCellForPixels(slot.Height * pixelsPerUnit), colors);
    if (modifier) {
      const float mainWidth = Menu::ButtonGlyphWidth(*glyph, slot.Height);
      const float modWidth = Menu::ButtonGlyphWidth(*modifier, slot.Height);
      const float plusX = slot.X - mainWidth * .5f - slot.Height * .3f;
      Menu::AppendButtonGlyph(output, *modifier, plusX - slot.Height * .3f - modWidth * .5f,
          slot.Y, slot.Height, slot.Color.alpha, 30,
          Menu::ButtonGlyphCellForPixels(slot.Height * pixelsPerUnit), colors);
      Menu::TextStyle text;
      text.Size = slot.Height; text.Align = Menu::TextAlign::Center; text.Layer = 30;
      text.Color = {1, 1, 1, slot.Color.alpha};
      Menu::AppendText(output, text, plusX, slot.Y + slot.Height * .3f, "+");
    }
  }
}

// Native UI materializations and decoded command lists are distinct events.
// Keep complete snapshots (including empty ones); never retain a hidden label.
// The adapter explicitly chooses whether submission lags materialization.
class NativeButtonOverlayFrames {
public:
  void Record(uint64_t throughDraw, std::vector<NativeButtonOverlaySlot> slots) {
    if (!mRecords.empty() && throughDraw < mRecords.back().Draw) Reset();
    if (!mRecords.empty() && throughDraw == mRecords.back().Draw) mRecords.pop_back();
    if (mRecords.size() == 32) mRecords.erase(mRecords.begin());
    mRecords.push_back({throughDraw, std::move(slots)});
  }
  void Select(uint64_t lastDraw, bool submittedNextUpdate) {
    mPresented.clear();
    for (std::size_t i = 0; i < mRecords.size(); ++i) {
      if (mRecords[i].Draw < lastDraw) continue;
      if (submittedNextUpdate && i == 0) return;
      mPresented = mRecords[i - (submittedNextUpdate ? 1 : 0)].Slots;
      return;
    }
  }
  void Reset() { mRecords.clear(); mPresented.clear(); }
  const auto& Presented() const { return mPresented; }
private:
  struct RecordValue { uint64_t Draw; std::vector<NativeButtonOverlaySlot> Slots; };
  std::vector<RecordValue> mRecords;
  std::vector<NativeButtonOverlaySlot> mPresented;
};
} // namespace Oot3dNativeGame
