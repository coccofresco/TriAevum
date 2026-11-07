#pragma once
#include "triaevum_title_presentation.h"
#include "../dekui/camera_live.h"
#include "../dekui/actor_draw_distance.h"
#include "../dekui/prop_draw_distance.h"
#include "../dekui/widescreen.h"
namespace Oot3dNativeGame {
inline void ApplyMm3dPresentationSettings(const TitlePresentationSettings& s) {
  namespace Camera=Mm3dDekui::CameraLive;
  Camera::renderer_owned.store(true);
  Camera::renderer_fov_multiplier.store(std::isfinite(s.FovMultiplier)?std::clamp(s.FovMultiplier,1.f,1.5f):1.f);
  Camera::horizontal_expansion.store(Camera::Expansion(s.HorizontalExpansion));
  Camera::vertical_expansion.store(Camera::Expansion(s.VerticalExpansion));
  Camera::desired_fov.store(45.f);
  Camera::desired_cutscene_fov.store(49.f);
  // No second zoom or fixed 16:9 owner. The actual picture policy reaches the
  // scene projection; dekui's camera logic only receives the widescreen fact.
  Mm3dDekui::Widescreen::camera_zoom_out.store(1.f);
  Mm3dDekui::Widescreen::supported_layout.store(true);
  Mm3dDekui::Widescreen::enabled.store(Camera::Expansion(s.HorizontalExpansion)>1.f);
  // The bounded dekui modules support 1..3x; these categories do not overlap.
  // This does not claim wider frustum admission or an object-bank extension.
  const float distance=s.ExtendedDrawDistance?3.f:1.f;
  Mm3dDekui::ActorDrawDistance::multiplier.store(distance);
  Mm3dDekui::PropDrawDistance::multiplier.store(distance);
}
}
