#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

// A temporal upscaler reconstructs at its colour input's extent, its render
// resolution. Its depth, motion and reactive guides keep the scene target's
// extent, which differs when a native display transfer resized the colour
// (OoT3D renders its top screen twice as wide and halves it). The guides are
// then resampled to the colour's extent: each output texel covers the source
// texels of its footprint, takes the depth and motion of the nearest of them
// and the largest reactive value.
//
// The guides are prepared for every temporal upscaler dispatch, resampled or
// not, because the upscalers take motion without the projection jitter. The
// scene's motion vectors (MotionVectors) carry it: a surface's motion is the
// difference of its jittered positions, and texels without motion (the sky,
// backdrops drawn without depth) hold zero, which against jittered motion
// reads as moving by the jitter's step and shakes the reconstruction.

struct UpscalerGuideSpan {
    uint32_t First = 0U;
    uint32_t Last = 0U;
};

// At most this many source texels per axis enter one output texel.
inline constexpr uint32_t kUpscalerGuideMaxSpan = 4U;

// The source texels [First, Last] that output texel `pixel` covers along one
// axis of an `extent`-texel output resampled from `sourceExtent` texels.
[[nodiscard]] UpscalerGuideSpan ResolveUpscalerGuideSpan(
    uint32_t pixel, uint32_t extent, uint32_t sourceExtent) noexcept;

[[nodiscard]] bool UpscalerGuidesNeedResample(
    uint32_t colorWidth, uint32_t colorHeight,
    uint32_t guideWidth, uint32_t guideHeight) noexcept;

// Projection jitter is applied in scene-target pixels; an upscaler takes it in
// pixels of its input.
[[nodiscard]] std::array<float, 2> UpscalerJitterInInputPixels(
    const std::array<float, 2>& targetPixels,
    uint32_t targetWidth, uint32_t targetHeight,
    uint32_t inputWidth, uint32_t inputHeight) noexcept;

// What a motion texel (uv offset to the previous frame, disocclusion in z)
// gains to leave the jitter out: the current frame's jitter minus the previous
// one's, in uv of the guides. Disoccluded texels hold no motion and keep none.
[[nodiscard]] std::array<float, 2> UpscalerMotionJitterCorrection(
    const std::array<float, 2>& currentJitterUv, const std::array<float, 2>& previousJitterUv) noexcept;
[[nodiscard]] std::array<float, 4> UpscalerMotionWithoutJitter(
    const std::array<float, 4>& motion, const std::array<float, 2>& correction) noexcept;

// Push constants of the guide shader.
struct UpscalerGuideConstants {
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t SourceWidth = 0U;
    uint32_t SourceHeight = 0U;
    float MotionCorrection[2] = {};
    uint32_t Padding[2] = {};
};
static_assert(sizeof(UpscalerGuideConstants) == 32);

// Bindings: 0 depth (larger is farther), 1 motion, 2 reactive (sampled);
// 3 R32F depth, 4 RGBA16F motion, 5 R16F reactive (storage); 6 sampler.
[[nodiscard]] std::string BuildUpscalerGuideResampleShader();

} // namespace Fast::Oot3d
