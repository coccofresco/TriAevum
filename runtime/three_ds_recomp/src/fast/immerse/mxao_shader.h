#pragma once

// iMMERSE module of TriAevum (see LICENSE.md in this directory).
// iMMERSE MXAO, ported from MartysMods_MXAO.fx.
// Copyright (c) Pascal Gilcher. All rights reserved.
// Not covered by TriAevum's GPL: this file stays under the original iMMERSE
// licence (LICENSE-iMMERSE.txt) and is used under the author's explicit
// permission, granted exclusively to TriAevum.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Immerse {

// The six MXAO programs (deinterleave, occlusion, filter, resolve, temporal,
// debug) in pass order. Every program includes the host's GLSL library
// `geometryLibrary` (pica_geometry_decode_normal of the geometry guide);
// debugLibrary is its AO debug view library (TriaevumImmerseHost).
[[nodiscard]] std::array<std::string, 6> BuildMxaoPassShaders(std::string_view debugLibrary,
                                                              std::string_view geometryLibrary);

// Pixels are deinterleaved into kMxaoTiles x kMxaoTiles tiles: a tile holds
// every fourth pixel of each axis, so the taps of one tile stay cache local.
inline constexpr uint32_t kMxaoTiles = 4U;

enum class MxaoMethod : uint32_t {
    GroundTruth = 0,
    SolidAngle = 1,
    VisibilityBitmask = 2,
    VisibilityBitmaskSolidAngle = 3,
};

// Slice/step presets of the original, Low through IDGAF.
inline constexpr uint32_t kMxaoQualityPresets = 7U;

// Filter flags of the regression passes.
inline constexpr uint32_t kMxaoFilterMirrored = 1U << 4U;
inline constexpr uint32_t kMxaoFilterCopy = 1U << 5U;
// Normals reconstructed from depth, as the original computes them
// (MXAO_USE_LAUNCHPAD_NORMALS 0); without it the renderer's normal guide.
inline constexpr uint32_t kMxaoDepthNormals = 1U << 6U;
// "Increase Radius with Distance": a constant screen radius, Response.w as a
// fraction of the width, instead of the world radius RadiusFade.x.
inline constexpr uint32_t kMxaoScreenRadius = 1U << 7U;
// Bits 8-11: temporal jitter phase added to every pixel's interleaved jitter.
inline constexpr uint32_t kMxaoJitterPhaseShift = 8U;
// Bits 12-13: shading rate (0 full, 1 half, 2 quarter); bits 14-15: this
// frame's phase. A skipped pixel keeps its previous estimate.
inline constexpr uint32_t kMxaoShadingRateShift = 12U;
inline constexpr uint32_t kMxaoShadingPhaseShift = 14U;
// The original's exponential fade: RadiusFade.z is its Fade Out Distance in
// depth normalized by the far plane (RadiusFade.w unused).
inline constexpr uint32_t kMxaoOriginalFade = 1U << 16U;
// Face normals of the renderer's geometry guide (binding 7), the exact plane
// of the visible triangle; normals from depth where the guide has none.
inline constexpr uint32_t kMxaoGeometryNormals = 1U << 17U;
// Bits 20-23: the view of the debug program (MxaoDebugProgramView).
inline constexpr uint32_t kMxaoDebugViewShift = 20U;

// Views written by the debug program into the visibility output: scalars in
// [0, 1], or a normal packed by the adapter's encoding (see
// BuildMxaoDebugShader).
enum class MxaoDebugProgramView : uint32_t {
    UsedNormals = 1,
    RendererNormals = 2,
    DepthNormals = 3,
    NormalDisagreement = 4,
    Depth = 5,
    EdgeWeight = 6,
    Fade = 7,
    HistoryLength = 8,
    // The geometry guide's face normals (packed), and their angle to the
    // normals from depth over 10 degrees (packed grey; magenta where the
    // guide has no face normal).
    FaceNormals = 9,
    FaceDisagreement = 10,
};

// Temporal accumulation: the history length an estimate converges to, the
// shortest one it keeps while moving, and the relative depth tolerance of a
// reprojected history sample.
inline constexpr float kMxaoTemporalHistoryLength = 16.0F;
inline constexpr float kMxaoTemporalMovingHistoryLength = 4.0F;
inline constexpr float kMxaoTemporalDepthTolerance = 0.02F;

// Successive frames visit the sixteen jitter phases in bit-reversed order, so
// any run of frames samples them evenly.
[[nodiscard]] constexpr uint32_t MxaoTemporalPhase(uint64_t frame) noexcept {
    const uint32_t phase = uint32_t(frame & 15U);
    return ((phase & 1U) << 3U) | ((phase & 2U) << 1U) | ((phase & 4U) >> 1U) | ((phase & 8U) >> 3U);
}

// View reconstruction is shared with the GTAO kernels: ViewX/ViewY map
// normalized storage UV to view XY / positive view depth, Depth linearizes
// the hardware depth. The adapter supplies orientation and projection.
struct MxaoConstants {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Method = 0;
    // Bits 0-2: quality preset; filter flags above.
    uint32_t Flags = 0;
    std::array<float, 4> ViewX{};
    std::array<float, 4> ViewY{};
    std::array<float, 4> Depth{};
    // World-space radius, bitmask thickness, fade start, fade end (view units).
    std::array<float, 4> RadiusFade{};
    // Amount, 1 / far plane (the filter's normalized depth), squared normal
    // decoding uncertainty, screen radius (fraction of the width).
    std::array<float, 4> Response{};
    // Temporal mode: history valid, maximum history length, relative depth
    // tolerance, minimum history length while moving. Zero outside the
    // temporal program.
    std::array<float, 4> Temporal{};
    // The previous frame's normalized depth of a view position p (MXAO's
    // projection space) is dot(PreviousDepth, vec4(p, 1)).
    std::array<float, 4> PreviousDepth{};
};
static_assert(sizeof(MxaoConstants) == 128);

// Adapter math for the temporal mode: the row of
// PreviousWorldToClip * inverse(CurrentWorldToClip) * Projection giving the
// previous view's linear depth (clip w) of a current view-space point, for
// MXAO's projection space p = (x, y, -z) and normalized by `farPlane`
// (column-major matrices).
[[nodiscard]] inline std::array<float, 4> MxaoPreviousDepthRow(const std::array<float, 16>& previousWorldToClip,
                                                              const std::array<float, 16>& inverseCurrentWorldToClip,
                                                              const std::array<float, 16>& projection,
                                                              float farPlane) noexcept {
    const auto multiply = [](const std::array<float, 16>& a, const std::array<float, 16>& b) {
        std::array<float, 16> result{};
        for (size_t column = 0; column < 4; ++column)
            for (size_t row = 0; row < 4; ++row)
                for (size_t k = 0; k < 4; ++k)
                    result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
        return result;
    };
    const auto viewToPreviousClip = multiply(multiply(previousWorldToClip, inverseCurrentWorldToClip), projection);
    const float scale = 1.0F / (farPlane > 1.0e-6F ? farPlane : 1.0e-6F);
    return { viewToPreviousClip[3] * scale, viewToPreviousClip[7] * scale, -viewToPreviousClip[11] * scale,
             viewToPreviousClip[15] * scale };
}

[[nodiscard]] constexpr uint32_t MxaoTiledExtent(uint32_t extent) noexcept {
    return (extent + kMxaoTiles - 1U) / kMxaoTiles * kMxaoTiles;
}

// Linear view depth of every pixel, written in tile order.
[[nodiscard]] std::string BuildMxaoDeinterleaveShader(std::string_view geometryLibrary);
// Occlusion per tile pixel: RG16F (ambient occlusion, normalized depth,
// negated where the filter must smooth across a depth discontinuity).
[[nodiscard]] std::string BuildMxaoOcclusionShader(std::string_view geometryLibrary);
// MXAO's depth-guided regression filter. The intermediate pass keeps the RG16F
// layout; the final pass writes the published R32F visibility (1 - occlusion).
[[nodiscard]] std::string BuildMxaoFilterShader(bool finalPass, std::string_view geometryLibrary);
// Debug views of the inputs the renderer passes and of the estimate's terms
// (not part of the original, whose only view is the AO itself). Binding 2 is
// the raw estimate, or the accumulation for the history view. `packNormal`
// supplies `float mxao_debug_pack_normal(vec3 view_normal)`, the display
// encoding of a normal in the R32F output.
[[nodiscard]] std::string BuildMxaoDebugShader(std::string_view packNormal, std::string_view geometryLibrary);
// Temporal accumulation of the per-pixel occlusion (not part of the original):
// reprojects the previous accumulation with the scene's motion vectors,
// rejects it where the surface's previous depth disagrees, clamps it to this
// frame's jitter phases and writes RGBA16F (occlusion, signed depth, history
// length) for the filter and the next frame. Bindings 5 and 6 are the motion
// vectors and the previous accumulation.
[[nodiscard]] std::string BuildMxaoTemporalShader(std::string_view geometryLibrary);

} // namespace Fast::Immerse
