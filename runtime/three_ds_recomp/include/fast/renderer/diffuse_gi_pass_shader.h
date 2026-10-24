#pragma once

// Compute kernels of the M5 runtime pass of the diffuse screen-space GI
// (docs/TRIAEVUM_SCREEN_SPACE_GI.md, "M5"). The kernels are the M2-M4 GLSL
// twins unchanged (normal pass, hierarchy level 0 and reduction, M4 gather,
// geometry pack, temporal accumulation, a-trous level) plus two own kernels
// that write the composition outputs: the output kernel (the pass's own
// extent) and the geometry-guided upsampler (a work-scale pass to the output
// extent). They are precompiled into builtin_pass.spvpack by
// build_pass_artifacts (the catalogue in fast/extension/builtin_pass_shaders.cpp
// appends BuildDiffuseGiPassKernels()); the pass resolves each kernel by its
// exact source text, never by name.
//
// Output kernel, set 0: 0 uniform OutputShaderParams; 1 vec4 colour[] (J.rgb,
// A raw per viewport pixel: the last a-trous level, or the temporal colour
// without a filter); 2 vec4 geometry[] (receivers: normal.xyz, view depth);
// 3 rgba16f image (J.rgb, receiver flag); 4 r32f image (A clamped to [0, 1],
// written only when requested). The images cover the whole buffer extent:
// texels outside the viewport, and every texel in the neutral mode, take
// J = 0, receiver 0, A = 1.
//
// Upsample kernel (work scale f > 1: the pass ran on one texel of every f x f
// block of the output, at a fixed phase), set 0: 0 uniform
// UpsampleShaderParams; 1 colour[] and 2 geometry[] of the work image; 3 uvec2
// output guide[] per output texel (view depth as float bits, the module's
// octahedral normal code; depth 0: no receiver); 4 vec4 shading geometry[] of
// the work image and 5 uint shading codes[] per output texel (M8: a work
// sample on the other side of a smooth crease of the same surface is accepted
// by the temporal module's smooth-crease rule); 6 rgba16f and 7 r32f images at
// the output extent. Each output receiver takes the bilinear taps of its
// continuous work position whose surface passes the plane and normal test
// against its own position and normal (weights renormalized over the accepted
// taps); with none, the nearest such sample of the surrounding 4 x 4; with
// none either, no estimate (J = 0, receiver 0, A = 1). Light never crosses a
// silhouette: a tap on another surface is rejected, not blended.
//
// Workgroups 8 x 8; no barriers, so out-of-range invocations return at once.

#include "fast/renderer/diffuse_gi_normal_code.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fast::Renderer::DiffuseGi {

enum class PassKernel : uint8_t {
    Normal,          // M2 geometric normal (renderer face normal or depth reconstruction)
    HierarchyLevel0, // M4 hierarchy level 0 with the radiance cache
    HierarchyReduce, // M4 hierarchy reduction, one dispatch per level
    Gather,          // M4 gather (metric radial, footprint strips, cache)
    Pack,            // M3 geometry pack (receivers for the temporal kernels)
    Temporal,        // M3 stratum reconstruction and temporal accumulation
    Filter,          // M3 a-trous level, one dispatch per level
    Output,          // M5 composition outputs at the pass's own extent (and the neutral outputs)
    Upsample,        // M5 geometry-guided upsampling of a work-scale pass to the output extent
};
inline constexpr uint32_t kPassKernelCount = 9;

struct PassKernelSource {
    PassKernel Kernel = PassKernel::Normal;
    const char* Name = ""; // pack name of the module (diagnostics only)
    std::string Source;    // exact GLSL text: the lookup key of the precompiled module
};

// The pass kernels in PassKernel order.
[[nodiscard]] std::vector<PassKernelSource> BuildDiffuseGiPassKernels();

inline constexpr uint32_t kOutputModeFinal = 0U;
inline constexpr uint32_t kOutputModeNeutral = 1U;
// Largest finite FP16 value: J is limited to it before the RGBA16F store so
// an HDR signal never becomes infinite (a storage bound, not an artistic clamp).
inline constexpr float kOutputHalfMax = 65504.0F;

// std140 uniform block of the output kernel, 48 bytes.
struct OutputShaderParams {
    std::array<uint32_t, 4> Extent{};   // buffer width, buffer height, viewport x, viewport y
    std::array<uint32_t, 4> Viewport{}; // viewport width, viewport height, mode, write A (1 / 0)
    std::array<float, 4> Limits{};      // largest stored |J|, 0, 0, 0
};
static_assert(sizeof(OutputShaderParams) == 48);

// std140 uniform block of the upsample kernel, 112 bytes.
struct UpsampleShaderParams {
    std::array<uint32_t, 4> Output{}; // output width, output height, write A (1 / 0), flags (1 shading, M8)
    std::array<uint32_t, 4> Work{};   // work width, work height, factor, phase
    std::array<float, 4> OutputX{}, OutputY{}; // output UV -> ray map (ax, bx, cx, 0; OutputX.w: hard-edge ratio, M8)
    std::array<float, 4> WorkX{}, WorkY{};     // work UV -> ray map
    std::array<float, 4> Surface{};   // plane sine, plane depth (relative), normal cos, largest stored |J|
};
static_assert(sizeof(UpsampleShaderParams) == 112);

[[nodiscard]] std::string BuildDiffuseGiOutputShader();
[[nodiscard]] std::string BuildDiffuseGiUpsampleShader();
// GLSL of the module's normal code (dgi_encode_normal, dgi_decode_normal),
// for the adapters that write the output guide.
[[nodiscard]] std::string DiffuseGiNormalCodecGlsl();

// The module's normal code (EncodeDiffuseGiNormal, DecodeDiffuseGiNormal):
// diffuse_gi_normal_code.h.

// FP32 twins of one output texel (output coordinates), in the operation order
// of the kernels. `colour` and `geometry` are viewport- (work-) sized vec4
// arrays; `guide` holds two uint per output texel.
struct OutputTexel {
    std::array<float, 4> Illumination{}; // J.rgb, receiver flag (before the FP16 store)
    float Accessibility = 1.0F;
};
[[nodiscard]] OutputTexel OutputTexelF32(const OutputShaderParams& params, uint32_t x, uint32_t y,
                                         const float* colour, const float* geometry) noexcept;
// M8 (Output[3] & 1): `shading` holds the work pixels' vec4 shading normals,
// `shadingGuide` the output texels' shading codes.
[[nodiscard]] OutputTexel UpsampleTexelF32(const UpsampleShaderParams& params, uint32_t x, uint32_t y,
                                           const float* colour, const float* geometry, const uint32_t* guide,
                                           const float* shading = nullptr,
                                           const uint32_t* shadingGuide = nullptr) noexcept;

} // namespace Fast::Renderer::DiffuseGi
