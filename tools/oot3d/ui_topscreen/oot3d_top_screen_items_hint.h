#pragma once

#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr std::string_view kTopScreen211MenuAtlasSemantic =
    "oot3d/topscreen/2.1.1/menu_atlas";

struct TopScreenItemsHintCursorBounds {
  bool Active = false;
  float CenterX = 0.0F;
  float MinimumY = 0.0F;
  float MaximumY = 0.0F;
  float Width = 0.0F;
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

// Exact source equivalent of the Items hint lane in TopScreen 2.1.1 payload
// function 0x005D2078. The texture regions, 90-update delay, 12-update fade,
// pulse waveform, cursor-relative placement, and 320-wide clamps come from
// the official EUR payload.
TopScreenItemsHintGeometry
BuildTopScreenItemsHintGeometry(const TopScreenItemsHintRuntimeState &state,
                                bool enabled) noexcept;

// Converts the 320x240 payload geometry into the promoted 400x240 UI canvas.
// The renderer receives only ordinary resolved UI primitives.
std::size_t AppendTopScreenItemsHintPresentation(
    const TopScreenItemsHintGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &menuAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output);

} // namespace Oot3dNativeGame
