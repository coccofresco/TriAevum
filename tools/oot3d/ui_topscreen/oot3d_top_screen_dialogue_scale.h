#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Oot3dNativeGame {

// TriAevum's dialogue size (TopScreenUiConfig::DialogueScalePercent).
//
// MessageWindow_Draw (0x004421B8, r0 the message context's window at +0x44)
// draws the window's 256 elements (+0x524). Each element's renderer (+0x64)
// holds the world matrix the draw uses (3 x 4 at +0x0C, canvas units, Y
// down): the box, the text, the icons and the ocarina staff alike. Its second
// renderer (+0x68, the text's shadow), drawn while +0x6E is set, holds one
// alike. For the window's draw only, every such matrix is scaled about the dialogue's
// pivot and restored when the draw returns (0x004421EC): the native layout,
// wrapping, timing and animation never see the change.
//
// The pivot is the centre of the screen in X (the boxes are centred) and the
// middle of the visible elements' origins in Y, so a box keeps its place on
// the screen and shrinks around it.
//
// Not applied while a pause page (Gear, Items, Map, System) is open: the
// item description uses the same window under the page, placed by the page.
inline constexpr std::uint32_t kTopScreenDialogueWindowOffset = 0x44U;
inline constexpr std::uint32_t kTopScreenDialogueElements = 0x524U;
inline constexpr std::uint32_t kTopScreenDialogueElementCount = 0x100U;
inline constexpr float kTopScreenDialoguePivotX = 200.0F;

struct TopScreenDialogueTransform {
  bool Active = false;
  float Scale = 1.0F;
  float PivotX = kTopScreenDialoguePivotX;
  float PivotY = 120.0F;
};

// The transform for the window at `window` (message context + 0x44) and the
// configured percent. Inactive at 100 percent, for a closed window, while a
// pause page is open or without a visible element. Reads only.
TopScreenDialogueTransform ResolveTopScreenDialogueTransform(NativeA32Memory &memory, std::uint32_t window,
                                                             std::uint8_t percent);

struct TopScreenDialogueScaleState {
  struct Saved {
    std::uint32_t Address = 0U;          // renderer + 0x0C
    std::array<std::uint32_t, 8> Words{}; // the matrix's X and Y rows
  };
  std::vector<Saved> Restore;
};

// Before the window's draw: restores anything an earlier draw left, then
// scales the elements' matrices. Returns whether it changed guest memory.
bool BeginTopScreenDialogueScale(NativeA32Memory &memory, std::uint32_t window, std::uint8_t percent,
                                 TopScreenDialogueScaleState &state);
// After the window's draw (and before any state is saved): the matrices as
// the native code left them.
void EndTopScreenDialogueScale(NativeA32Memory &memory, TopScreenDialogueScaleState &state);

} // namespace Oot3dNativeGame
