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
  case TopScreenHudButton::L: return NativeControlAction::L;
  case TopScreenHudButton::R: return NativeControlAction::R;
  case TopScreenHudButton::Select: return NativeControlAction::Select;
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

void TopScreenNativeLabelFrames::Record(TopScreenNativeLabelRecord record) {
  if (!mRecords.empty() && record.ThroughDraw <= mRecords.back().ThroughDraw) {
    if (record.ThroughDraw == mRecords.back().ThroughDraw) {
      // No new draw since the last read: the same frame, read again.
      mRecords.back() = std::move(record);
      return;
    }
    mRecords.clear();
  }
  if (mRecords.size() == kCapacity) mRecords.erase(mRecords.begin());
  mRecords.push_back(std::move(record));
}

const TopScreenNativeLabelRecord *TopScreenNativeLabelFrames::ForFrame(std::uint64_t lastDraw) const noexcept {
  // The guest submits a frame's command list on the update after the one
  // that built it, so the drain that decodes a frame's last draw reads the
  // next update's state: the frame's own state is the drain before it
  // (measured: a staff note's glyph then fades in with the native note).
  for (std::size_t index = 0; index < mRecords.size(); ++index) {
    if (mRecords[index].ThroughDraw >= lastDraw) return index == 0U ? nullptr : &mRecords[index - 1U];
  }
  return nullptr;
}

const TopScreenNativeLabelRecord *TopScreenNativeLabelFrames::Decoding(std::uint64_t lastDraw) const noexcept {
  for (const auto &record : mRecords)
    if (record.ThroughDraw >= lastDraw) return &record;
  return nullptr;
}

void TopScreenNativeLabelFrames::SetLatestActionLetter(std::optional<TopScreenHudLabelSlot> letter) {
  if (!mRecords.empty()) mRecords.back().ActionLetter = std::move(letter);
}

TopScreenActionLetterPair TopScreenActionLetterPairing::Next(const TopScreenActionLetterReads &reads) {
  // The reads in order; only the first and the latest are kept (a third read
  // in one record is the latest's neighbour).
  std::uint64_t used = 0U;
  const auto take = [&]() -> std::optional<TopScreenHudLabelSlot> {
    return used++ == 0U ? reads.First : reads.Last;
  };
  TopScreenActionLetterPair pair;
  if (mPreviousWaits && reads.Count != 0U) {
    pair.FillsPrevious = true;
    pair.Previous = take();
  }
  mPreviousWaits = false;
  if (mHasCarried) {
    pair.Current = std::move(mCarried);
  } else if (used < reads.Count) {
    pair.Current = take();
  } else {
    // This record's update materializes after it: the next read is its own.
    mPreviousWaits = true;
  }
  mCarried.reset();
  mHasCarried = used < reads.Count;
  if (mHasCarried) mCarried = reads.Last;
  return pair;
}

TopScreenHudLabelStats ApplyTopScreenHudButtonGlyphs(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                    const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                    const TopScreenHudButtonGlyphs &glyphs,
                                                    float pixelsPerCanvasUnit,
                                                    const std::vector<TopScreenHudLabelSlot> &nativeOverlays) {
  return ReplaceTopScreenHudButtonLabels(
      hud, actionLetter,
      [&](const TopScreenHudLabelSlot &slot, std::vector<oot3d::ui::UiPrimitive> &output) {
        const auto index = static_cast<std::size_t>(slot.Button);
        if (index >= glyphs.size() || !glyphs[index]) return false;
        const auto cell = Menu::ButtonGlyphCellForPixels(slot.Height * pixelsPerCanvasUnit);
        const float centerX =
            slot.AlignRight ? slot.CenterX - Menu::ButtonGlyphWidth(*glyphs[index], slot.Height) * 0.5F
                            : slot.CenterX;
        Menu::AppendButtonGlyph(output, *glyphs[index], centerX, slot.CenterY, slot.Height, slot.Color.alpha,
                                slot.Layer, cell, TopScreenHudButtonGlyphColors(slot.Color));
        return true;
      },
      nativeOverlays);
}

} // namespace Oot3dNativeGame
