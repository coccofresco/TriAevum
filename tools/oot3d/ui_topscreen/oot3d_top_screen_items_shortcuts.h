#pragma once

#include "oot3d_top_screen_config.h"
#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

using TopScreenItemsShortcutOrder = std::array<std::uint8_t, 4>;
inline constexpr TopScreenItemsShortcutOrder kTopScreenItemsShortcutIdentity{0, 1, 2, 3};

// 3.0 0x005E2CB4 / 0x005E2D24, table 0x00609AF8. Preferences stay on the host.
inline TopScreenItemsShortcutOrder ValidateTopScreenItemsShortcutOrder(
    const TopScreenItemsShortcutOrder &order) noexcept {
  unsigned seen = 0;
  for (const auto value : order) {
    if (value > 3 || (seen & (1U << value)) != 0) return kTopScreenItemsShortcutIdentity;
    seen |= 1U << value;
  }
  return order;
}
inline TopScreenDpadAction ResolveTopScreenItemsShortcutAction(
    const TopScreenItemsShortcutOrder &order, unsigned row) noexcept {
  constexpr std::array actions{TopScreenDpadAction::MinimapToggle, TopScreenDpadAction::View,
                              TopScreenDpadAction::Ocarina, TopScreenDpadAction::AllBootsToggle};
  return actions[ValidateTopScreenItemsShortcutOrder(order)[row < 4 ? row : 0]];
}

struct TopScreenItemsShortcutState {
  TopScreenItemsShortcutOrder Order = kTopScreenItemsShortcutIdentity;
  int SelectedRow = -1;
  int MovingRow = -1;
  int SwapTarget = -1;
  int SwapFrame = -1;
  int HoldFrames = -1;
  int MovingY = 0, TargetY = 0;
  unsigned PulseFrame = 0;
  bool SelectionSettled = false;
};

struct TopScreenItemsShortcutInput {
  bool Active = false;
  unsigned Column = 0, Row = 0;
  std::uint32_t Directions = 0, Pressed = 0, Held = 0;
  // Per physical row, already resolved through Order and native ownership/age.
  std::array<bool, 4> Available{};
  int TouchRow = -1;
};

struct TopScreenItemsShortcutUpdate {
  std::uint32_t ConsumeDirections = 0, ConsumeButtons = 0;
  bool Entered = false, Left = false, CursorChanged = false, OrderChanged = false;
  unsigned Column = 0, Row = 0;
  TopScreenDpadAction Action = TopScreenDpadAction::None;
  enum class Sound { None, Move, Decide, Cancel } UiSound = Sound::None;
};

// 0x005E460C: run after native input sampling, before Items navigation. The
// native five-column grid uses column 5 for the four assignment slots.
inline TopScreenItemsShortcutUpdate AdvanceTopScreenItemsShortcuts(
    TopScreenItemsShortcutState &state, const TopScreenItemsShortcutInput &input) noexcept {
  TopScreenItemsShortcutUpdate out;
  out.Column = input.Column;
  out.Row = input.Row;
  const auto clearSelection = [&] {
    const auto order = state.Order;
    state = {};
    state.Order = order;
  };
  if (!input.Active) {
    out.Left = state.SelectedRow >= 0;
    clearSelection();
    return out;
  }
  if (state.SelectedRow >= 0 &&
      (input.Column != 0 || input.Row != static_cast<unsigned>(state.SelectedRow))) {
    out.Left = true;
    clearSelection();
  }
  const int vertical = ((input.Directions & 0x80U) != 0) - ((input.Directions & 0x40U) != 0);
  const auto select = [&](unsigned row) {
    state.SelectedRow = static_cast<int>(row & 3U);
    state.HoldFrames = -1;
    state.SelectionSettled = false;
    out.CursorChanged = true;
    out.Column = 0;
    out.Row = static_cast<unsigned>(state.SelectedRow);
    out.UiSound = TopScreenItemsShortcutUpdate::Sound::Move;
  };
  if (state.SelectedRow < 0) {
    const bool enter = (input.Column == 0 && (input.Directions & 0x20U)) ||
                       (input.Column == 5 && (input.Directions & 0x10U));
    if (!enter && input.TouchRow < 0) return out;
    select(input.TouchRow >= 0 ? static_cast<unsigned>(input.TouchRow)
                              : (input.Row + vertical + 4U) & 3U);
    out.Entered = true;
    out.Left = false;
    out.ConsumeDirections = 0xF0U;
    return out;
  }
  if (state.SwapFrame >= 0) {
    out.ConsumeDirections = 0xF0U;
    out.ConsumeButtons = 0xF03U;
    if (state.SwapFrame < 6) {
      state.MovingY += (state.SwapTarget * 48 + 7 - state.MovingY) / 2;
      state.TargetY += (state.MovingRow * 48 + 7 - state.TargetY) / 2;
    } else if (state.SwapFrame == 6) {
      const auto value = state.Order[state.MovingRow];
      state.Order[state.MovingRow] = state.Order[state.SwapTarget];
      state.Order[state.SwapTarget] = value;
      state.MovingRow = -1;
      state.SelectionSettled = true;
      out.OrderChanged = true;
    } else {
      state.SwapFrame = -1;
      state.SwapTarget = -1;
      return out;
    }
    ++state.SwapFrame;
    return out;
  }
  out.ConsumeButtons = 0xC01U; // A, X, Y must not assign the native grid item underneath.
  if (state.MovingRow >= 0) {
    ++state.PulseFrame;
    out.ConsumeButtons |= 0x300U;
    if (input.Pressed & 2U) {
      state.MovingRow = -1;
      state.HoldFrames = -1;
      out.ConsumeButtons |= 2U;
      out.UiSound = TopScreenItemsShortcutUpdate::Sound::Cancel;
    }
  }
  if (state.HoldFrames < 0) {
    if (input.Pressed & 1U) state.HoldFrames = 0;
  } else if ((input.Held & 1U) == 0) {
    state.HoldFrames = -1;
    if (state.MovingRow < 0) {
      state.MovingRow = state.SelectedRow;
      state.PulseFrame = 0;
      out.UiSound = TopScreenItemsShortcutUpdate::Sound::Decide;
    } else if (state.MovingRow == state.SelectedRow) {
      state.MovingRow = -1;
      out.UiSound = TopScreenItemsShortcutUpdate::Sound::Cancel;
    } else {
      state.SwapTarget = state.SelectedRow;
      state.MovingY = state.MovingRow * 48 + 7;
      state.TargetY = state.SwapTarget * 48 + 7;
      state.SwapFrame = 0;
      out.UiSound = TopScreenItemsShortcutUpdate::Sound::Decide;
    }
  } else if (state.MovingRow < 0 && ++state.HoldFrames > 14) {
    state.HoldFrames = -1;
    if (input.Available[state.SelectedRow])
      out.Action = ResolveTopScreenItemsShortcutAction(state.Order, state.SelectedRow);
    return out;
  }
  if (state.MovingRow >= 0) out.ConsumeDirections |= 0x30U;
  else if (input.Directions & 0x30U) {
    // Let the native horizontal step run once: 5 + right -> grid 0;
    // 0 + left -> assignment 5. This avoids the old direct grid/right wrap.
    out.Column = (input.Directions & 0x10U) ? 5U : 0U;
    out.Row = static_cast<unsigned>(state.SelectedRow);
    out.CursorChanged = out.Left = true;
    out.ConsumeDirections = 0xC0U;
    clearSelection();
    return out;
  }
  if (input.TouchRow >= 0 && input.TouchRow != state.SelectedRow) {
    select(static_cast<unsigned>(input.TouchRow));
  } else if (vertical != 0) {
    select(static_cast<unsigned>(state.SelectedRow + vertical + 4) & 3U);
    out.ConsumeDirections |= 0xC0U;
  }
  return out;
}

struct TopScreenItemsShortcutDrawState {
  bool Active = false;
  bool Dimmed = false;
  bool Child = false;
  bool BootsOwned = false;
  std::uint8_t Ocarina = 0xFF;
  std::uint8_t Boots = 0x44;
};

inline bool TopScreenItemsShortcutAvailable(TopScreenDpadAction action,
                                            const TopScreenItemsShortcutDrawState &draw) noexcept {
  if (action == TopScreenDpadAction::Ocarina) return draw.Ocarina != 0xFF;
  if (action == TopScreenDpadAction::AllBootsToggle) return draw.BootsOwned && !draw.Child;
  return action == TopScreenDpadAction::MinimapToggle || action == TopScreenDpadAction::View;
}

inline constexpr std::string_view kTopScreen30UiAtlasSemantic = "oot3d/topscreen/3.0/ui_atlas";

// 0x005E2DF4 / 0x005CB3B4, on the 400x240 canvas (projection 0.58).
// Native item atlas is 512x512; custom_ui01 is 512x256. No native draw
// resources or mod-private model addresses are required by presentation.
std::size_t AppendTopScreenItemsShortcutPresentation(
    const TopScreenItemsShortcutState &state, const TopScreenItemsShortcutDrawState &draw,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

} // namespace Oot3dNativeGame
