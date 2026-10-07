#pragma once

#include <cstdint>

namespace Oot3dNativeGame {

// Majora's Mask 3D (code.bin 6eec27b1..., v1.0): code addresses its adapter
// uses. Each was recovered from the binary and is checked at its use.

// Builds a 4x4 perspective frustum matrix: r0 = output matrix, s0..s5 =
// left, right, bottom, top, near, far (hard-float), stores -1 at [r0+0x38].
// The same engine routine as OoT3D's 0x002FDE9C, without its rotation
// argument: a separate routine (0x00122EFC, r1 = pivot) rotates a matrix in
// place for the portrait LCD. Its first instructions:
//   vldr s6, [pc, #0x8c] (0xED9F3A23); vsub.f32 s11, s3, s2; vadd.f32 s8, s3, s2;
//   vsub.f32 s13, s1, s0; vadd.f32 s9, s1, s0
inline constexpr uint32_t kMm3dMtx44Frustum = 0x00122FE8U;
inline constexpr uint32_t kMm3dMtx44FrustumFirstWord = 0xED9F3A23U;

// That builder makes the stereo camera's base frustum (object 0x006B35D4:
// a constant 0.08 x 0.048 window at near 80, far 1000), not the scene's
// field of view, which the game's view matrix carries. The scene camera is
// the play state's View, laid out as N64 MM's View (include/z64view.h):
// fovy, zNear, zFar, scale at +0x18, eye at +0x28, at at +0x34, up at
// +0x40. It is a static object (.bss) at this address. Its fovy and clip
// planes give the scene's projection; its eye and target hold constant
// values (60000, ...), not the live camera, which is still to be found.
inline constexpr uint32_t kMm3dPlayViewAddress = 0x0072F5D0U;
inline constexpr uint32_t kMm3dViewFovyOffset = 0x18U;
inline constexpr uint32_t kMm3dViewNearOffset = 0x1CU;
inline constexpr uint32_t kMm3dViewFarOffset = 0x20U;

} // namespace Oot3dNativeGame
