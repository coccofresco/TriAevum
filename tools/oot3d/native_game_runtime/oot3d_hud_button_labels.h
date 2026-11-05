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
  // The latest record's A letter, given once its materialization has run
  // (TopScreenActionLetterPairing); nothing without a record.
  void SetLatestActionLetter(std::optional<TopScreenHudLabelSlot> letter);
  void Reset() noexcept { mRecords.clear(); }
  [[nodiscard]] std::size_t Size() const noexcept { return mRecords.size(); }

private:
  static constexpr std::size_t kCapacity = 32U;
  std::vector<TopScreenNativeLabelRecord> mRecords;
};

// The A letters for a new label record: its own, and the previous record's
// when that one was still waiting for its materialization.
struct TopScreenActionLetterPair {
  bool FillsPrevious = false;
  std::optional<TopScreenHudLabelSlot> Previous;
  std::optional<TopScreenHudLabelSlot> Current;
};

// Pairs Quest materializations with label records one to one, in order. A
// host refresh does not hold exactly one guest update: an update split across
// two refreshes leaves one record with no materialization and a neighbour
// with two. Measured in Hyrule Field: the record comes first and its update
// materializes in the next refresh, so the empty record waits for the next
// materialization; a surplus read (the other order) is carried to the next
// record. Either way a letter waits or is carried one record only and always
// comes from a materialization that ran, so a HUD that stops materializing
// leaves no letter behind.
class TopScreenActionLetterPairing {
public:
  [[nodiscard]] TopScreenActionLetterPair Next(const TopScreenActionLetterReads &reads);
  void Reset() noexcept {
    mCarried.reset();
    mHasCarried = false;
    mPreviousWaits = false;
  }

private:
  std::optional<TopScreenHudLabelSlot> mCarried;
  bool mHasCarried = false;
  bool mPreviousWaits = false;
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
