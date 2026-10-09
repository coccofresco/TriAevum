#pragma once

#include "fast/renderer3ds/pica_light_binding.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_environment_lighting.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <string>

namespace Fast::Renderer3ds {

struct alignas(16) PicaSurfaceLightUniforms {
    std::array<float, 4> DirectionEnabled{};
    // RGB diffuse; W: point-shadow atlas row of a point light, negative for none.
    std::array<float, 4> Diffuse{};
    // A positive radius identifies a point light; zero preserves the native direction.
    std::array<float, 4> PositionRadius{};
};

struct alignas(16) PicaScenePointLightUniforms {
    std::array<float, 4> PositionRadius{};
    // RGB colour; W: point-shadow atlas row, negative for none.
    std::array<float, 4> Color{};
};

// A placed light: position in view space (as the scene points), axis and
// tangent in world space.
struct alignas(16) PicaPlacedLightUniforms {
    // XYZ: view-space position; W: reach (world units).
    std::array<float, 4> PositionReach{};
    // RGB irradiance when Control.w bit1 is set, otherwise colour times reflectance.
    // W: point-shadow atlas row, negative for none.
    std::array<float, 4> Color{};
    // XYZ: world emission axis; W: PicaPlacedLightKind, plus 8 when two-sided.
    std::array<float, 4> AxisKind{};
    // Rectangle: XYZ the world width axis times the half width, W the half
    // height. Disc: XYZ any in-plane axis times the radius. Spot: X, Y the
    // outer and inner cosines.
    std::array<float, 4> TangentShape{};
};

// Point lights with a shadow cube map this sample: atlas row i belongs to
// Identities[i] (zero for an empty row).
struct PicaPointShadowAssignment {
    std::array<uint64_t, 8> Identities{};
    // range (fraction of the radius), strength, face texels, border texels
    std::array<float, 4> Parameters{};
    // 1/width, 1/height, near-plane fraction of the far plane, enabled
    std::array<float, 4> Atlas{};
};

inline constexpr uint32_t kMaximumPicaSurfaceWaterBodies = 4U;

// Bounds overlap only identifies a possibly mixed mesh. The surface shader
// decides water per fragment (height, footprint and face normal) and keeps
// its native colour there; dry fragments still use the fragment-light operand.
// Require a complete water set for that decision. Unknown/oversized metadata
// and whole-water draws retain their existing route. The legacy capability-
// false path, including OoT's generated sources and keys, is unchanged.
[[nodiscard]] inline bool PicaNativeFragmentOperandWaterEligible(
    bool capability, uint32_t waterCount, bool boundsKnown, bool reachesWater, bool wholeWater) noexcept {
    if (!capability || wholeWater || waterCount > kMaximumPicaSceneWaterBodies) return false;
    if (waterCount == 0U) return !boundsKnown;
    if (!boundsKnown) return false;
    return !reachesWater || waterCount <= kMaximumPicaSurfaceWaterBodies;
}

struct alignas(16) PicaSurfaceLightingUniforms {
    std::array<float, 4> MaterialDiffuse{};
    std::array<float, 4> MaterialAmbient{};
    // Resolved ambient; material-weighted unless native primary owns material (W bit1).
    std::array<float, 4> ResolvedAmbient{};
    // W: bit 0 preserve native brightness, bit 1 native primary owns material,
    // bits 8-15 placed-light count. Bit 1 is only for fragment-lit Lambert.
    std::array<uint32_t, 4> Control{};
    std::array<std::array<float, 4>, 3> ViewToWorldLinear{};
    std::array<PicaSurfaceLightUniforms, 3> Lights{};
    std::array<float, 4> EnvironmentParameters{}; // native local, effective ambient gain, fog, enabled
    std::array<float, 4> EnvironmentAmbient{};
    std::array<std::array<float, 4>, 2> EnvironmentDirections{};
    std::array<std::array<float, 4>, 2> EnvironmentColors{};
    std::array<std::array<float, 4>, kMaximumPicaSceneDirectionalLights> SceneDirections{};
    std::array<std::array<float, 4>, kMaximumPicaSceneDirectionalLights> SceneDirectionalColors{};
    std::array<PicaScenePointLightUniforms, kMaximumPicaScenePointLights> ScenePoints{};
    // Gains of the direct light that seeds global illumination: native scene
    // directionals, added key light, dynamic lights (native local lights,
    // Navi and fairies). Written by the renderer; zero publishes no source.
    std::array<float, 4> GlobalIlluminationSources{};
    // Point-shadow parameters and atlas of the assigned rows (zero: none).
    std::array<float, 4> PointShadow{};
    std::array<float, 4> PointShadowAtlas{};
    // Placed lights; Control[3] bits 8-15 hold their count.
    std::array<PicaPlacedLightUniforms, kMaximumPicaPlacedLights> Placed{};
    // Water bodies under this draw (AssignPicaSurfaceWater): the view's
    // translation (xyz; w = body count, plus 8 when the whole draw is water),
    // each body's world footprint (min x, min z, max x, max z) and the
    // bodies' surface heights. A fragment on one of those surfaces, or any
    // fragment of a whole-water draw, keeps its native colour.
    std::array<float, 4> WaterViewTranslation{};
    std::array<std::array<float, 4>, kMaximumPicaSurfaceWaterBodies> WaterFootprints{};
    std::array<float, 4> WaterHeights{};
    // Water optics: F0, GGX alpha, environment reflection and direct
    // specular strengths (kPicaSurfaceWaterOptics unless a scene sets them).
    std::array<float, 4> WaterOptics{};
    // Water ripples: time (seconds, wrapped), slope strength, wavelength scale
    // and a tall waterfall's base height (foam where it lands; -1e9: none).
    std::array<float, 4> WaterRipples{};
    // Water flow: texture coordinate 0's scrolling (texture units per second,
    // xy) and whether it is known (z); w: the reflection trace owns the
    // water's environment term (the Hi-Z trace finds water surfaces).
    std::array<float, 4> WaterFlow{};
    // Movers' ripple rings (PicaWaterRippleField): centre x, z, birth (water
    // ripple time) and strength (0: none).
    std::array<std::array<float, 4>, kPicaWaterRings> WaterRings{};
};

static_assert(sizeof(PicaSurfaceLightingUniforms) == 2912U);

// Water's Fresnel reflectance at normal incidence, its GGX alpha, and its
// environment reflection (subdued: the environment is the fog colour until
// screen-space reflections) and direct specular strengths.
inline constexpr std::array<float, 4> kPicaSurfaceWaterOptics{0.02F, 0.08F, 0.6F, 1.0F};
// Ripple slope strength and wavelength scale (1: waves of 157 to 17 world units).
inline constexpr std::array<float, 2> kPicaSurfaceWaterRipples{1.0F, 1.0F};

// Scene lighting requires the column-major WorldToView captured with this
// geometry sample. Neither a live camera nor a model/bone matrix is a substitute.
// A validated camera snapshot may be shared by all materials in the same view.
struct PicaSurfaceLightingView {
    std::array<float, 16> WorldToView{};
    std::array<std::array<float, 4>, 3> Rows{};
    std::array<std::array<float, 4>, 3> ViewToWorldLinear{};
};

std::optional<PicaSurfaceLightingView> BuildPicaSurfaceLightingView(
    const std::array<float, 16>& worldToView) noexcept;

// Float-uniform bytes contain consecutive native vec4 values. Their enclosing
// packed-buffer layout belongs to the caller, not to the surface evaluator.
[[nodiscard]] std::optional<PicaSurfaceLightingUniforms> BuildPicaSurfaceLightingUniforms(
    const PicaVertexShaderHookLayout& hooks, uint32_t booleanUniforms,
    std::span<const uint8_t> floatUniformBytes,
    const PicaNativeLightBinding& binding,
    const PicaEnvironmentLighting& environment,
    const PicaSurfaceLightingView* cameraView,
    const PicaPointShadowAssignment* pointShadows = nullptr,
    bool nativeFragmentLambert = false) noexcept;

// Once the actual variant is known, restore the legacy material-weighted
// inputs for PBR. A requested PBR mode with no material still uses Lambert.
void FoldPicaSurfaceMaterialForPbr(PicaSurfaceLightingUniforms& uniforms) noexcept;

// Removes the scene directional lights a draw's native per-pixel (fragment)
// lighting already applies: those with the direction (view space, toward
// the light) of one of its native directional lights. They are not added
// again; a shadow covering one applies to the native light instead
// (PicaSurfaceLightingBody, receiver slots 0-7 = native lights). Returns how
// many were removed.
uint32_t ExcludePicaSceneDirectionalsLitNatively(
    PicaSurfaceLightingUniforms& uniforms,
    std::span<const std::array<float, 3>> nativeDirectionalViewDirections) noexcept;

// The water bodies a draw's world bounds reach (the largest overlaps first;
// at most kMaximumPicaSurfaceWaterBodies): water drawn in a mixed room mesh is
// found per fragment, on a body's surface with a horizontal face. A whole
// water draw (a catalog water texture) is water on every face; its fragments
// on a body's surface are those the reflection trace finds.
void AssignPicaSurfaceWater(PicaSurfaceLightingUniforms& uniforms, const PicaSurfaceLightingView& view,
                            const PicaSceneWaterBodies& bodies, const std::array<float, 3>& worldMinimum,
                            const std::array<float, 3>& worldMaximum, float seconds,
                            bool wholeDraw = false,
                            const std::array<PicaWaterRing, kPicaWaterRings>* rings = nullptr) noexcept;

[[nodiscard]] std::string PicaSurfaceLightingDeclarations(bool fragmentOperand = false);
// Last of the material guide writes: a water fragment marks the guide (B at
// 1.0, a value reflection weights never take: pica_encode_reflection_weight
// stops at 254/255), keeping the reactive alpha; the reflection trace frames
// the drawn water's edges by it.
[[nodiscard]] std::string_view PicaSurfaceLightingWaterMarker() noexcept;
[[nodiscard]] std::string PicaSurfaceLightingBody(bool fragmentOperand = false);
// After the native TEV, before fog: water optics on a water fragment's
// native colour (the body marks the fragment).
[[nodiscard]] std::string_view PicaSurfaceLightingWaterShading() noexcept;


} // namespace Fast::Renderer3ds
