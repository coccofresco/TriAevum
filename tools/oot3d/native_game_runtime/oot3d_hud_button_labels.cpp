#include "oot3d_hud_button_labels.h"

#include <algorithm>

namespace Oot3dNativeGame {

NativeControlAction TopScreenHudButtonAction(TopScreenHudButton button) noexcept {
  switch (button) {
  case TopScreenHudButton::A: return NativeControlAction::A;
  case TopScreenHudButton::B: return NativeControlAction::B;
  case TopScreenHudButton::X: return NativeControlAction::X;
  case TopScreenHudButton::Y: return NativeControlAction::Y;
  case TopScreenHudButton::Zl: return NativeControlAction::Zl;
  case TopScreenHudButton::Zr: return NativeControlAction::Zr;
  case TopScreenHudButton::Count: break;
  }
  return NativeControlAction::Count;
}

bool TopScreenHudButtonGlyphsActive(TopScreenHudButtonLabels setting, NativePromptSet set) noexcept {
  return setting == TopScreenHudButtonLabels::Controller && set != NativePromptSet::Touch;
}

TopScreenHudButtonGlyphs ResolveTopScreenHudButtonGlyphs(NativePromptSet set, const NativeControlConfig &controls) {
  TopScreenHudButtonGlyphs glyphs;
  for (std::size_t index = 0; index < glyphs.size(); ++index) {
    const auto action = TopScreenHudButtonAction(static_cast<TopScreenHudButton>(index));
    auto binding = controls.Bindings[static_cast<std::size_t>(action)];
    if (!controls.MouseEnabled) binding.Mouse = NativeMouseButton::None;
    glyphs[index] = Menu::ResolveButtonGlyph(set, binding);
  }
  return glyphs;
}

Menu::ButtonGlyphColors TopScreenHudButtonGlyphColors(const oot3d::ui::UiColor &label) noexcept {
  // Black and white like every button glyph: the white body takes only the
  // label's own dimming (its colour), the ink stays black.
  Menu::ButtonGlyphColors colors;
  const float light = std::min({label.red, label.green, label.blue});
  colors.Body = {light, light, light, 1.0F};
  colors.Ink = {0.0F, 0.0F, 0.0F, 1.0F};
  return colors;
}

TopScreenHudLabelStats ApplyTopScreenHudButtonGlyphs(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                    const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                    const TopScreenHudButtonGlyphs &glyphs,
                                                    float pixelsPerCanvasUnit) {
  return ReplaceTopScreenHudButtonLabels(
      hud, actionLetter, [&](const TopScreenHudLabelSlot &slot, std::vector<oot3d::ui::UiPrimitive> &output) {
        const auto index = static_cast<std::size_t>(slot.Button);
        if (index >= glyphs.size() || !glyphs[index]) return false;
        const auto cell = Menu::ButtonGlyphCellForPixels(slot.Height * pixelsPerCanvasUnit);
        Menu::AppendButtonGlyph(output, *glyphs[index], slot.CenterX, slot.CenterY, slot.Height, slot.Color.alpha,
                                slot.Layer, cell, TopScreenHudButtonGlyphColors(slot.Color));
        return true;
      });
}

} // namespace Oot3dNativeGame
