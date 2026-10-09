#pragma once

#include "fast/oot3d/world_debug_lines.h"
#include "oot3d_scene_environment.h"
#include <array>
#include <cstdint>
#include <vector>

namespace Oot3dNativeGame {

// World units the translation and rotation axes reach from the selected
// light, each way: farther than any room, so they meet the scene's surfaces.
inline constexpr float kLightGizmoAxisReach = 20000.0F;

// The placed lights' gizmos, in world space:
// - every light's shape in its colour (the selected one bright): omni and
//   point their reach (three great circles), spot its cone, surface its
//   rectangle or disc with its emitting side(s) and reach, sun a sheaf of
//   arrows along its light; hidden lights faint;
// - the selected light's translation axes (world X red, Y green, Z blue)
//   and rotation axes (yaw about Y; pitch about its right axis, orange; roll
//   about its own axis, cyan, for a surface), with rings of a constant size
//   on screen around it, the eye given.
// Without a light of `selectedLight`, the first light is the selected one.
[[nodiscard]] Fast::Oot3d::WorldDebugLineSet
BuildPlacedLightGizmo(const std::vector<PlacedLight> &lights, uint32_t selectedLight,
                      const std::array<float, 3> &eye);

} // namespace Oot3dNativeGame
