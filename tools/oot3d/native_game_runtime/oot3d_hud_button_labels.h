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

// The native letters a native frame draws (the staff's notes, the Items
// page's slot labels, the A ring's letter), read from guest memory at the end
// of the guest update that built that frame, so the glyphs drawn over them
// belong to the frame being presented, not to the latest guest update (the
// presented native frame lags the guest by its submission pipeline).
struct TopScreenNativeLabelRecord {
  // The last draw submission decoded by the end of that update.
  std::uint64_t ThroughDraw = 0U;
  std::vector<TopScreenHudLabelSlot> StaffNotes;
  std::vector<TopScreenHudLabelSlot> ItemsLabels;
  std::optional<TopScreenHudLabelSlot> ActionLetter;
};

class TopScreenNativeLabelFrames {
public:
  // One record per guest update with native draws; draw ids only grow (a
  // savestate load that restarts them restarts the history).
  void Record(TopScreenNativeLabelRecord record);
  // The record of the update that built a frame whose last draw is
  // `lastDraw`: the guest submits a frame's command list on the next update,
  // so it is the record before the first one through that draw. Nothing
  // when none was kept.
  [[nodiscard]] const TopScreenNativeLabelRecord *ForFrame(std::uint64_t lastDraw) const noexcept;
  // The record of the update that decoded that frame's last draw (the one
  // after the frame's own). A page that leaves its rest in that update is
  // already moving in the frame presented: its labels are not drawn there.
  [[nodiscard]] const TopScreenNativeLabelRecord *Decoding(std::uint64_t lastDraw) const noexcept;
  void Reset() noexcept { mRecords.clear(); }
  [[nodiscard]] std::size_t Size() const noexcept { return mRecords.size(); }

private:
  static constexpr std::size_t kCapacity = 32U;
  std::vector<TopScreenNativeLabelRecord> mRecords;
};

// Replaces the labels in `hud` (final canvas coordinates: the HUD, the pause
// hints, the ocarina song box) and draws the A glyph over the native ring and
// the staff's note glyphs over the native notes. `pixelsPerCanvasUnit` picks
// the atlas size.
TopScreenHudLabelStats ApplyTopScreenHudButtonGlyphs(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                    const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                    const TopScreenHudButtonGlyphs &glyphs,
                                                    float pixelsPerCanvasUnit,
                                                    const std::vector<TopScreenHudLabelSlot> &nativeOverlays = {});

} // namespace Oot3dNativeGame
