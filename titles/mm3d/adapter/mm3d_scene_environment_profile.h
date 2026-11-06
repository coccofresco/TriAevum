#pragma once

// MM3D's base scene environment per environment group: the profile a scene
// takes while neither the user nor the release has a file for it.
//
// Owner decision Q-P1 (b), 2026-11-06: an OoT3D-style base profile per group,
// applied by function. MM3D's scene is lit by native fragment lights, so the
// base is the shape those draws support (Fast::Renderer3ds::
// PicaFragmentBalanceProfileSupported):
// - Scene lights (the environment's sun/moon records, which the lighting
//   probe classifies as Scene) are replaced: the main one becomes a white 1x
//   key that follows it and casts the shadows; the other one, native scene
//   direct 0, is removed (OoT3D's base, not preserving scene brightness).
// - The scene ambient (each draw's native global ambient) is scaled by
//   OoT3D's base native ambient strength, 0.6: a relative gain, so the game's
//   own room/actor ambient balance stays.
// - Local lights (lamps, actor and room lights, anything unproved) keep their
//   native light: native local strength 1.
// - No added ambient, fill, rim or placed light; native fog; native caster
//   faces.
// The per-vertex information stays native: the vertex colour (the baked
// shading and fake AO) enters MM3D's room geometry in a later TEV stage, which
// the profile never touches, and draws without native fragment lighting take
// OoT3D's baked-reference path (native colour as the lit reference).
//
// Indoor takes the same base, as OoT3D's base applies to its interiors too
// (owner amendment: static-light interiors use the equation OoT3D uses for
// them). It acts by function there as well: MM3D's interior room geometry has
// no scene directional among its fragment lights (only the ambient and local
// lamps), so those draws keep their native lighting, while actors that carry
// the environment's records take the key and its shadow. No interior shells
// are requested until MM3D supplies them (P2.6). Unknown keeps the native
// lighting: an unproved environment is never replaced.

#include "oot3d_scene_environment.h"

#include <optional>

namespace Oot3dNativeGame {

inline constexpr float kMm3dBaseNativeAmbientStrength = 0.6F;

[[nodiscard]] inline std::optional<SceneEnvironmentProfile> Mm3dBaseSceneEnvironmentProfile(
    SceneEnvironmentGroup group) {
  switch (group) {
  case SceneEnvironmentGroup::Indoor:
  case SceneEnvironmentGroup::TimeFlows:
  case SceneEnvironmentGroup::TimeAtEntry:
  case SceneEnvironmentGroup::FixedTime:
    break;
  default:
    return std::nullopt;
  }
  auto profile = NativePreservingSceneEnvironmentProfile();
  profile.Enabled = true;
  profile.PreserveSceneBrightness = false;
  profile.NativeDirectStrength = 0.0F;
  profile.NativeLocalStrength = 1.0F;
  profile.NativeAmbientStrength = kMm3dBaseNativeAmbientStrength;
  profile.AmbientColor = {1.0F, 1.0F, 1.0F};
  profile.AmbientIntensity = 0.0F;
  profile.FogStrength = 1.0F;
  profile.Lights = {{{45.0F, 45.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
                     {-135.0F, 30.0F, {1.0F, 1.0F, 1.0F}, 0.0F}}};
  profile.KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection::SceneMain;
  profile.KeyAzimuthOffset = 0.0F;
  profile.KeyElevationOffset = 0.0F;
  profile.PlacedLights.clear();
  profile.Rim.Enabled = false;
  profile.CasterFaces = Fast::Renderer3ds::ShadowCasterFaces::Native;
  return profile;
}

} // namespace Oot3dNativeGame
