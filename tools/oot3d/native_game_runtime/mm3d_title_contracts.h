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
// the current View, laid out as N64 MM's View (include/z64view.h): fovy,
// zNear, zFar, scale at +0x18, eye at +0x28, at at +0x34, up at +0x40.
// 0x001DD0C4 returns it: the pointer at 0x00656B5C (set by 0x00170514; the
// play state's View, on the heap, during gameplay) or, while that is null,
// the static View at 0x0072F5D0, which View_Init (0x001393E0) leaves at
// fovy 60, near 5, far 12800 and a placeholder eye and target (60000, ...).
inline constexpr uint32_t kMm3dCurrentViewPointer = 0x00656B5CU;
inline constexpr uint32_t kMm3dDefaultViewAddress = 0x0072F5D0U;
inline constexpr uint32_t kMm3dViewFovyOffset = 0x18U;
inline constexpr uint32_t kMm3dViewNearOffset = 0x1CU;
inline constexpr uint32_t kMm3dViewFarOffset = 0x20U;
inline constexpr uint32_t kMm3dViewEyeOffset = 0x28U;
inline constexpr uint32_t kMm3dViewAtOffset = 0x34U;
// The View's world-to-view matrix (Mtx34, rows): it maps the eye to the
// origin and the view direction to -Z.
inline constexpr uint32_t kMm3dViewWorldToViewOffset = 0x17CU;

// The play state (N64 MM's PlayState): Play_Init calls View_Init on
// play + 0x1F8 (0x0012AA40), so the play state's View is the current View
// during gameplay. Its LightContext list head is at play + 0xA88: nodes
// {LightInfo* info, prev, next}, as N64 MM's LightNode. The environment's
// two directional lights (N64 MM's envCtx.dirLight1 and dirLight2) are the
// LightInfo records at play + 0x86D0 and play + 0x86E8, laid out as OoT3D's:
// type u8 at +0 (1 = directional), direction s8 x, y, z at +4 (toward the
// light, scaled by 127), color u8 r, g, b at +7. The scene draws light them
// per pixel: every opaque scene draw enables PICA fragment lighting with two
// directional lights.
inline constexpr uint32_t kMm3dPlayViewOffset = 0x1F8U;
inline constexpr uint32_t kMm3dPlayLightListHeadOffset = 0xA88U;
inline constexpr uint32_t kMm3dPlayEnvironmentLight1Offset = 0x86D0U;
inline constexpr uint32_t kMm3dPlayEnvironmentLight2Offset = 0x86E8U;

// The top screen's canvas: a 400x240 display drawn into a rotated 240x400
// viewport (480x400 at the doubled resolution) at the colour buffer's
// corner, its display width along native Y. Its interface is laid out
// (mm3d-dekui's poses) for a 16:9 frame in which the 400:240 screen keeps its
// proportions at the centre: the frame is 16/15 of the screen along native Y.
inline constexpr float kMm3dTopScreenCanvasAspect = 400.0F / 240.0F;
inline constexpr float kMm3dUiFrameAspect = 16.0F / 9.0F;
inline constexpr float kMm3dUiFrameExtensionY = kMm3dUiFrameAspect / kMm3dTopScreenCanvasAspect;
template <class Viewport>
constexpr bool IsMm3dTopScreenCanvas(const Viewport& viewport) noexcept {
    return viewport.CornerX == 0 && viewport.CornerY == 0 && viewport.HalfHeight == 200.0F &&
           (viewport.HalfWidth == 120.0F || viewport.HalfWidth == 240.0F);
}

} // namespace Oot3dNativeGame
