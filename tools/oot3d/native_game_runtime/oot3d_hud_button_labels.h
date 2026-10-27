#pragma once

// The TopScreen HUD's button labels as the glyphs of the inputs bound to
// them. Three owners meet here: Input's controls owner gives the bindings and
// the prompt family of the device in use (NativeControlConfigRuntime),
// TopScreen owns the setting (TopScreenUiConfig::HudButtonLabels) and finds
// the labels (oot3d_top_screen_button_labels.h), and the menu's glyph art
// draws them (oot3d_menu_button_glyphs.h). Called once per HUD composition,
// so a change of device or of a binding shows at the next frame.

#include "oot3d_menu_button_glyphs.h"
#include "oot3d_native_control_config.h"
#include "oot3d_top_screen_button_labels.h"

#include <array>
#include <optional>
#include <vector>

namespace Oot3dNativeGame {

using TopScreenHudButtonGlyphs = std::array<std::optional<Menu::ButtonGlyph>, kTopScreenHudButtonCount>;

// The console button each HUD label names, in the controls owner's actions.
[[nodiscard]] NativeControlAction TopScreenHudButtonAction(TopScreenHudButton button) noexcept;

// Whether the HUD's labels change at all. With Original, or with touch (the
// on-screen controls own their letters), the HUD is left untouched.
[[nodiscard]] bool TopScreenHudButtonGlyphsActive(TopScreenHudButtonLabels setting, NativePromptSet set) noexcept;

// Each HUD button's glyph in the family; nothing where that device has no
// binding (the label stays). The mouse counts only while it is enabled.
[[nodiscard]] TopScreenHudButtonGlyphs ResolveTopScreenHudButtonGlyphs(NativePromptSet set,
                                                                     const NativeControlConfig &controls);

// Black and white: a white body (dimmed as the label is) and black ink.
[[nodiscard]] Menu::ButtonGlyphColors TopScreenHudButtonGlyphColors(const oot3d::ui::UiColor &label) noexcept;

// Replaces the labels in `hud` (final HUD canvas coordinates) and draws the
// A glyph over the native ring. `pixelsPerCanvasUnit` picks the atlas size.
TopScreenHudLabelStats ApplyTopScreenHudButtonGlyphs(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                    const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                    const TopScreenHudButtonGlyphs &glyphs,
                                                    float pixelsPerCanvasUnit);

} // namespace Oot3dNativeGame
