// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// MM3D native-HUD touch anchors: the MM counterpart of OoT3D TopScreen's
// ExtractTopScreenTouchControls placement (HUD group centre -> the touch
// control's content centre, scaled to fill 0.9 of it). OoT3D reads its
// groups from host-drawn TopScreen quads; MM3D draws its HUD natively, so
// the groups are the live native panes Mm3dButtonOverlay already validates
// (mm3d-dekui NativeInventory ABI, EUR v1.1 widget emission 134DE4):
//   AActionIconBase/bb00_ybtn (38x38 ring)  -> A
//   BActionIcon/a_button_base_pane (38x38)  -> B
//   HudItemIconSet|ItemIconSet2/disp_x_pane -> X, disp_y_pane -> Y
//   .../disp_1_pane (Restoration I, ZR+X)   -> ZL control
//   .../disp_2_pane (Restoration II, ZR+Y)  -> ZR control
//   HudDpad/pane_dpad                       -> D-pad
// Group anchors (dekui RestorationHud::Install): action buttons and item set
// scale about (373,15) and shift right by the 16:9 margin, the D-pad about
// (20,42) shifting left, so live panes, never constants, give the centres.
// The I/II -> ZL/ZR assignment is a touch-layout choice (a touch control has
// no chord); see OVERLAY RESULTS owner questions.
#include "three_ds_touch_anchors.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace Oot3dNativeGame {

enum class Mm3dTouchHudElement : unsigned char { ActionA, ActionB, ItemX, ItemY, ItemI, ItemII, Dpad, Count };
inline constexpr std::size_t kMm3dTouchHudElementCount = static_cast<std::size_t>(Mm3dTouchHudElement::Count);

inline constexpr ThreeDsRecomp::Input::TouchAnchor Mm3dTouchHudAnchor(Mm3dTouchHudElement element) noexcept {
  using ThreeDsRecomp::Input::TouchAnchor;
  switch (element) {
  case Mm3dTouchHudElement::ActionA: return TouchAnchor::A;
  case Mm3dTouchHudElement::ActionB: return TouchAnchor::B;
  case Mm3dTouchHudElement::ItemX: return TouchAnchor::X;
  case Mm3dTouchHudElement::ItemY: return TouchAnchor::Y;
  case Mm3dTouchHudElement::ItemI: return TouchAnchor::Zl;
  case Mm3dTouchHudElement::ItemII: return TouchAnchor::Zr;
  case Mm3dTouchHudElement::Dpad: return TouchAnchor::Dpad;
  default: return TouchAnchor::Count;
  }
}

// A native HUD group in the centred 16:9 canvas (240 high, x shifted by the
// 40/3 pillar like the button overlay's slots).
struct Mm3dTouchHudGroup {
  bool Known = false;
  float X = 0.0F, Y = 0.0F, Radius = 0.0F;
};
using Mm3dTouchHudGroups = std::array<Mm3dTouchHudGroup, kMm3dTouchHudElementCount>;

struct Mm3dTouchHudPlacement {
  bool Active = false;
  float FromX = 0.0F, FromY = 0.0F, ToX = 0.0F, ToY = 0.0F, Scale = 1.0F;
  // A native point of this group, moved into its touch control.
  void Apply(float& x, float& y) const noexcept {
    if (!Active) return;
    x = ToX + (x - FromX) * Scale;
    y = ToY + (y - FromY) * Scale;
  }
};
using Mm3dTouchHudPlan = std::array<Mm3dTouchHudPlacement, kMm3dTouchHudElementCount>;

// Same fill as OoT3D TopScreen (kTopScreenTouchFill).
inline constexpr float kMm3dTouchHudFill = 0.9F;

// Shape: the shared touch-skin resolver (ResolveTopScreenTouchControlShape)
// or any callable with its signature returning ContentX/ContentY/ContentHalf.
template<class ShapeResolver>
Mm3dTouchHudPlan PlanMm3dTouchHud(const Mm3dTouchHudGroups& groups,
                                  const ThreeDsRecomp::Input::TouchAnchorSet& anchors, float canvasWidth,
                                  ShapeResolver&& shapeOf) {
  Mm3dTouchHudPlan plan{};
  if (!std::isfinite(canvasWidth) || !(canvasWidth > 0.0F)) return plan;
  for (std::size_t index = 0; index < kMm3dTouchHudElementCount; ++index) {
    const auto control = Mm3dTouchHudAnchor(static_cast<Mm3dTouchHudElement>(index));
    const auto& anchor = anchors[static_cast<std::size_t>(control)];
    const auto& group = groups[index];
    if (!anchor.Present || !group.Known || !std::isfinite(group.X) || !std::isfinite(group.Y) ||
        !std::isfinite(group.Radius) || !(group.Radius > 0.0F))
      continue;
    const auto shape = shapeOf(control, anchor, canvasWidth);
    if (!(shape.ContentHalf > 0.0F)) continue;
    plan[index] = {true, group.X, group.Y, shape.ContentX, shape.ContentY,
                   shape.ContentHalf * kMm3dTouchHudFill / group.Radius};
  }
  return plan;
}

// Consumer: the plan moves native HUD draws through dekui's HudButtons
// world-matrix transform (hud_buttons.h, indices a_action_button,
// icon_item_x_g, icon_item_y_g, b_action_button, icon_item_1_g,
// icon_item_2_g). The D-pad (HudDpad) has no HudButtons group: it stays
// native (not relocated) - see OVERLAY RESULTS.
inline constexpr int Mm3dTouchHudButtonIndex(Mm3dTouchHudElement element) noexcept {
  switch (element) {
  case Mm3dTouchHudElement::ActionA: return 0;
  case Mm3dTouchHudElement::ItemX: return 1;
  case Mm3dTouchHudElement::ItemY: return 2;
  case Mm3dTouchHudElement::ActionB: return 3;
  case Mm3dTouchHudElement::ItemI: return 4;
  case Mm3dTouchHudElement::ItemII: return 5;
  default: return -1;
  }
}

// The placement as a native 400x240 world mapping (canvas x = native x +
// 40/3): native' = Scale * native + (X, Y).
struct Mm3dTouchHudNativeMapping {
  bool Active = false;
  float Scale = 1.0F, X = 0.0F, Y = 0.0F;
};
inline Mm3dTouchHudNativeMapping Mm3dTouchHudNative(const Mm3dTouchHudPlacement& placement) noexcept {
  constexpr float pillar = 40.0F / 3.0F;
  if (!placement.Active) return {};
  return {true, placement.Scale, placement.ToX - pillar - placement.Scale * (placement.FromX - pillar),
          placement.ToY - placement.Scale * placement.FromY};
}

} // namespace Oot3dNativeGame
