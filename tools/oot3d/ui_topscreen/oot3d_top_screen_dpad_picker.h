#pragma once

#include "oot3d_top_screen_items_shortcuts.h"
#include "oot3d_top_screen_items_hint.h"

namespace Oot3dNativeGame {

using TopScreenDpadAssignments = std::array<TopScreenDpadAction, 4>;

enum class TopScreenDpadPickerPhase { Closed, Opening, Choosing, Confirming, Closing };
struct TopScreenDpadPickerState {
  TopScreenDpadPickerPhase Phase = TopScreenDpadPickerPhase::Closed;
  TopScreenDpadAction Action = TopScreenDpadAction::None;
  TopScreenDpadAssignments Assignments{};
  bool Child = false;
  // 0x005D2014 source 3 (0x0060ECBC): opened from the Gear page, so
  // 0x005CD22C keeps it only while Gear page state is 2.
  bool Gear = false;
  unsigned Frame = 0, PulseFrame = 0;
  int Direction = -1, ClearHoldFrames = -1;
  std::uint32_t PreviousHeld = 0;
  bool Active() const noexcept { return Phase != TopScreenDpadPickerPhase::Closed; }
};
struct TopScreenDpadPickerUpdate {
  bool Changed = false;
  std::uint32_t Sound = 0;
  // 0x005D2CF0: a new Y with no direction or A/B asks for the View Model.
  bool ViewModel = false;
};

// 3.0 0x005D2014(action | 0x1000, 0), with age-local host preferences.
inline void OpenTopScreenDpadPicker(TopScreenDpadPickerState &state,
                                    TopScreenDpadAction action, bool child,
                                    const TopScreenDpadAssignments &assignments,
                                    std::uint32_t held) noexcept {
  state = {};
  state.Phase = TopScreenDpadPickerPhase::Opening;
  state.Action = action;
  state.Child = child;
  state.Assignments = assignments;
  state.PreviousHeld = held;
}

// 0x005D29E4: physical D-pad edges confirm directly. Circle-pad repeats do
// not choose a direction. A/B closes; X held for 45 subsequent updates clears
// all four mappings. 0x005D0604 swaps the first duplicate with the old slot.
inline TopScreenDpadPickerUpdate AdvanceTopScreenDpadPicker(
    TopScreenDpadPickerState &state, bool active, bool child, std::uint32_t held) noexcept {
  TopScreenDpadPickerUpdate out;
  if (!active || child != state.Child) { state = {}; return out; }
  if (!state.Active()) return out;
  const auto pressed = held & ~state.PreviousHeld;
  state.PreviousHeld = held;
  ++state.Frame;
  ++state.PulseFrame;
  using Phase = TopScreenDpadPickerPhase;
  if (state.Phase == Phase::Opening) {
    if (state.Frame >= 8) { state.Phase = Phase::Choosing; state.Frame = 0; }
  } else if (state.Phase == Phase::Confirming) {
    if (state.Frame >= 12) { state.Phase = Phase::Closing; state.Frame = 0; }
  } else if (state.Phase == Phase::Closing) {
    if (state.Frame >= 6) state = {};
  } else {
    if ((held & 0x400U) == 0) state.ClearHoldFrames = -1;
    else if (state.ClearHoldFrames < 0) {
      if (pressed & 0x400U) state.ClearHoldFrames = 0;
    } else if (++state.ClearHoldFrames > 44) {
      for (auto &action : state.Assignments) action = TopScreenDpadAction::None;
      state.ClearHoldFrames = -2;
      out.Changed = true;
      out.Sound = 0x01000496U;
    }
    constexpr std::array<std::uint32_t, 4> directions{0x40U, 0x80U, 0x20U, 0x10U};
    int direction = -1;
    for (unsigned i = 0; i < directions.size(); ++i)
      if (pressed & directions[i]) { direction = static_cast<int>(i); break; }
    if (direction >= 0) {
      state.Direction = direction;
      state.Frame = 0;
      auto &destination = state.Assignments[direction];
      if (destination == state.Action) {
        destination = TopScreenDpadAction::None;
        state.Direction = -1;
        state.Phase = Phase::Closing;
        out.Sound = 0x0100048FU;
      } else {
        for (unsigned i = 0; i < state.Assignments.size(); ++i) {
          if (i != static_cast<unsigned>(direction) && state.Assignments[i] == state.Action) {
            state.Assignments[i] = destination;
            break;
          }
        }
        destination = state.Action;
        state.Phase = Phase::Confirming;
        out.Sound = 0x0100048DU;
      }
      out.Changed = true;
    } else if (pressed & 3U) {
      state.Phase = Phase::Closing;
      state.Frame = 0;
      out.Sound = 0x01000496U;
    } else if (pressed & 0x800U) {
      out.ViewModel = true;
    }
  }
  return out;
}

// Native item ids for the current age, indexed by TopScreenDpadAction. None,
// View and Map are drawn separately; 0xFF means the mapped item is not owned.
struct TopScreenDpadPickerDrawState {
  std::array<std::uint8_t, 14> Items{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  // 0x005D289C: the picked item has a View Model, so the legend adds [Y].
  bool ViewModel = false;
};
// 0x005CCA90 / 0x005CBD64: free quads on native slot 5 (OcarinaPage),
// linear min/mag, clamp-to-edge, top-left UVs. RGB is the vertex colour;
// alpha is clamp(sample.a * Color.alpha * TextureAlphaScale, 0, 1).
// Apply the scale AFTER filtering; do not bake it into texels or vertex alpha.
struct TopScreenDpadPickerCursorVertex { float X, Y, U, V; };
struct TopScreenDpadPickerCursorQuad {
  std::array<TopScreenDpadPickerCursorVertex, 4> Vertices;
  oot3d::ui::UiColor Color;
};
struct TopScreenDpadPickerHoldCursor {
  std::array<TopScreenDpadPickerCursorQuad, 48> Quads{};
  unsigned Count = 0;
  float CenterX = 0, CenterY = 0;
  float TextureAlphaScale = 1.0F;
};
TopScreenDpadPickerHoldCursor BuildTopScreenDpadPickerHoldCursor(
    float centerX, float centerY, float progress) noexcept;

// 0x005CD470 / 0x005E34A4: shortcut A-hold progress beside the live native
// cursor, visible on held updates 2..14; update 15 opens the picker.
TopScreenDpadPickerHoldCursor BuildTopScreenItemsShortcutHoldCursor(
    const TopScreenItemsShortcutState &state,
    const TopScreenItemsHintCursorBounds &bounds) noexcept;
std::size_t AppendTopScreenItemsShortcutHoldPresentation(
    const TopScreenDpadPickerHoldCursor &holdCursor,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

std::size_t AppendTopScreenDpadPickerPresentation(
    const TopScreenDpadPickerState &state, const TopScreenDpadPickerDrawState &draw,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output,
    TopScreenDpadPickerHoldCursor *holdCursor = nullptr);

} // namespace Oot3dNativeGame
