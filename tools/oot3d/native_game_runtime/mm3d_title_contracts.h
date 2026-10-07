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

} // namespace Oot3dNativeGame
