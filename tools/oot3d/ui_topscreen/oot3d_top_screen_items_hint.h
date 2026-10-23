#pragma once

#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

struct TopScreenItemsHintCursorBounds {
  bool Active = false;
  float CenterX = 0.0F;
  float MinimumY = 0.0F;
  float MaximumY = 0.0F;
  float Width = 0.0F;
  // Canvas X of native X 0 under the cursor's projection: 80 at 0.6 and
  // 84 at 0.58. 0x005E0E6C draws the hint with the cursor's projection.
  float ScreenOffsetX = 80.0F;
};

struct TopScreenItemsHintRuntimeState {
  TopScreenItemsHintCursorBounds Cursor;
  float PreviousCenterX = 0.5F;
  float PreviousMinimumY = 0.5F;
  std::uint16_t StableFrames = 0U;
  std::uint32_t AnimationFrame = 0U;
  bool Tracking = false;
};

// Advances only on an OoT3D guest update. Presentation-only frames reuse the
// latest state so visual interpolation cannot accelerate the native delay.
void AdvanceTopScreenItemsHintState(
    TopScreenItemsHintRuntimeState &state,
    const TopScreenItemsHintCursorBounds &cursor) noexcept;

struct TopScreenItemsHintQuad {
  bool Visible = false;
  float X = 0.0F;
  float Y = 0.0F;
  float Width = 0.0F;
  float Height = 0.0F;
  float AtlasX = 0.0F;
  float AtlasY = 0.0F;
  float AtlasWidth = 0.0F;
  float AtlasHeight = 0.0F;
  float Alpha = 0.0F;
};

struct TopScreenItemsHintGeometry {
  bool Visible = false;
  std::array<TopScreenItemsHintQuad, 2> Quads{};
};

// SingleScreen 3.0 0x005E0E6C: SELECT and ? from custom_ui01, placed on the
// upper 400x240 canvas with the native timing and clamps.
TopScreenItemsHintGeometry
BuildTopScreenItemsHintGeometry(const TopScreenItemsHintRuntimeState &state,
                                bool enabled) noexcept;

// Emits the upper-screen geometry directly on the 400x240 UI canvas.
// The renderer receives only ordinary resolved UI primitives.
std::size_t AppendTopScreenItemsHintPresentation(
    const TopScreenItemsHintGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

// SingleScreen 3.0 0x005E2BA8 -> 0x005E9A58: "[X] View Model" below the Gear
// cursor. `fade` counts the updates past the 90-update delay and `pulse`
// every update; `cursor` comes from ReadTopScreenGearCursorBounds.
std::size_t AppendTopScreenGearViewModelLegendPresentation(
    const TopScreenItemsHintCursorBounds &cursor, std::uint32_t fade, std::uint32_t pulse,
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

// SingleScreen 3.0 0x005E9C3C: the View Model control line, drawn instead of
// every other 0x005E0E6C overlay while the View Model is open.
std::size_t AppendTopScreenViewModelControlsPresentation(
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

} // namespace Oot3dNativeGame
