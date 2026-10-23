#pragma once

#include "oot3d_top_screen_items_hint.h"

#include <string>

namespace Oot3dNativeGame {

class NativeA32Memory;

// Application-side adapter for the original OoT3D Items owner. It reproduces
// the selected-model lookup and four-quad bounds scan of 3.0 0x005CB298,
// then projects the cursor onto the upper canvas as in 0x005E3264.
bool ReadTopScreenItemsHintCursorBounds(const NativeA32Memory &memory,
                                        TopScreenItemsHintCursorBounds *bounds,
                                        std::string *error = nullptr);

// 3.0 0x005E2BA8: the same scan of the Gear cursor owner 0x00504480, placed
// with the legend's projection 0.8 (native X + 40) of 0x005E9A58.
bool ReadTopScreenGearCursorBounds(const NativeA32Memory &memory,
                                   TopScreenItemsHintCursorBounds *bounds,
                                   std::string *error = nullptr);

// 3.0 0x005E0F80: the native Gear slot layout (0x00504BD4: X, then Y, width
// and height arrays of 27) and the A-hold ring beside it, at projection 0.8
// (native X + 40): centre X + width + 12.133 - 2, Y + height / 2.
bool ReadTopScreenGearHoldRingCenter(const NativeA32Memory &memory, std::uint32_t slot,
                                     float *centerX, float *centerY);

} // namespace Oot3dNativeGame
