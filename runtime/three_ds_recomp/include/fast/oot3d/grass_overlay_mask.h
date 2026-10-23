#pragma once

#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_tev_program.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include <array>
#include <memory>
#include <functional>
#include <span>
#include <vector>

namespace Fast::Oot3d {

// Static room coverage, evaluated at placement time. One world unit is 1 cm.
inline constexpr float kGrassOverlayPlaneTolerance = 1.0F;
inline constexpr uint64_t kGrassOverlayVersion = 2;
struct GrassOverlayAlpha {
    // Invoked on a private worker copy only, after the persistent placement miss.
    std::function<void(GrassOverlayAlpha&)> LoadTextures;
    Renderer3ds::PicaTevProgram Program;
    std::array<std::array<float, 4>, 6> Constants{};
    std::array<float, 4> Buffer{};
    // R/G/B/A of native texture unit zero, only channels the alpha path needs.
    std::array<std::shared_ptr<const GrassScalarMask>, 4> Texture;
    GrassTextureWrap WrapS = GrassTextureWrap::Repeat, WrapT = GrassTextureWrap::Repeat;
    bool Linear = false;
    bool Translucent = false;
    bool AlphaTest = false;
    Renderer3ds::PicaCompareFunction Compare = Renderer3ds::PicaCompareFunction::Always;
    uint8_t Reference = 0;
    uint64_t Version = 0;
    bool NativeSupported = true;
    GrassMaskRule Rule;
    std::shared_ptr<const GrassScalarMask> CustomMask;
};
// Alpha-only dependency analysis. Unsupported fragment-light/procedural or
// RGB-previous dependencies are declined, never approximated as opaque.
struct GrassOverlayAlphaInputs {
    bool Supported = true;
    bool Primary = false;
    uint8_t TextureChannels = 0;
};
struct GrassOverlayPrimaryAlpha {
    bool Valid = false;
    uint8_t InputRegister = 0xff;
    float Scale = 1;
};
[[nodiscard]] GrassOverlayPrimaryAlpha
DecodeGrassOverlayPrimaryAlpha(const Renderer3ds::PicaVertexShaderHookLayout& hooks, std::span<const uint8_t> uniforms);
[[nodiscard]] GrassOverlayAlphaInputs GrassOverlayRequiredAlphaInputs(const GrassOverlayAlpha& alpha);
[[nodiscard]] float EvaluateGrassOverlayCoverage(const GrassOverlayAlpha& alpha, const std::array<float, 2>& uv,
                                                 float primaryAlpha);
[[nodiscard]] float EvaluateGrassOverlayNativeCoverage(const GrassOverlayAlpha& alpha, const std::array<float, 2>& uv,
                                                       float primaryAlpha);
[[nodiscard]] float SampleGrassOverlayTexture(const GrassScalarMask& mask, const std::array<float, 2>& uv,
                                              const GrassOverlayAlpha& alpha);
[[nodiscard]] uint64_t GrassOverlayAlphaVersion(const GrassOverlayAlpha& alpha, uint64_t textureIdentity);

struct GrassOverlayVertex {
    std::array<float, 3> Position{};
    std::array<float, 2> Uv{};
    float Alpha = 1;
};
struct GrassOverlayTriangle {
    std::array<GrassOverlayVertex, 3> Vertices;
    std::shared_ptr<const GrassOverlayAlpha> Alpha;
    uint64_t Draw = 0;
    std::array<float, 3> Low{}, High{}, Normal{}, Edge1{}, Edge2{};
    float D11 = 0, D12 = 0, D22 = 0, InverseDeterminant = 0;
};
struct GrassOverlayFootprint {
    GrassTextureSelector Texture;
    GrassTextureSelector Source;
    uint64_t Draw = 0;
    uint64_t NativeIdentity = 0;
    GrassMaskMode Mode = GrassMaskMode::Automatic;
    bool NativeSupported = true;
    // Area-weighted, deterministic quadrature over actual clipped overlap.
    double Area = 0;
    double CoverageArea = 0;
    std::array<double, 16> TextureAlphaArea{};
    std::array<double, 16> NativeAlphaArea{};
    std::vector<std::array<std::array<float, 3>, 3>> Triangles;
};
// Clip an overlay onto a source triangle and accumulate its footprint/alpha.
void MeasureGrassOverlayFootprint(const std::array<std::array<float, 3>, 3>& source,
                                  const GrassOverlayTriangle& overlay, GrassOverlayFootprint& footprint);

struct GrassOverlaySet {
    // Every later coplanar draw: what the mask editor measures and lists.
    uint64_t Version = 0;
    // What a placement depends on: the source and only the overlays that can
    // remove grass. Off and unsupported Automatic overlays stay listed without
    // rebuilding placements while they animate (dusk mist scrolls every frame).
    uint64_t PlacementVersion = 0;
    uint64_t SourceVersion = 0;
    // The frame a change of the covering overlays produced this placement
    // version; zero for the first set of a source.
    uint64_t PlacementChangedFrame = 0;
    std::vector<GrassOverlayTriangle> Triangles;
};
// False when the overlay's coverage is zero everywhere: mode Off, or
// Automatic on a native alpha path the evaluator does not support.
[[nodiscard]] bool GrassOverlayCanCover(const GrassOverlayAlpha& alpha) noexcept;
// Rejects degenerate/non-finite triangles; prepares camera-independent bounds
// and barycentrics once, before the parallel candidate walk.
[[nodiscard]] bool PrepareGrassOverlayTriangle(GrassOverlayTriangle& triangle);
[[nodiscard]] std::vector<const GrassOverlayTriangle*>
GrassOverlaysForTriangle(const std::array<std::array<float, 3>, 3>& source, const GrassOverlaySet& overlays);
// Triangles within a draw form a union, draws compose by alpha-over. Return
// survival probability (1-coverage), not a hard UV-rectangle cutout.
[[nodiscard]] float GrassOverlaySurvival(const std::array<float, 3>& point,
                                         std::span<const GrassOverlayTriangle* const> triangles);

[[nodiscard]] std::shared_ptr<const GrassOverlaySet>
ResolveGrassOverlayTextures(std::shared_ptr<const GrassOverlaySet> set);

} // namespace Fast::Oot3d
