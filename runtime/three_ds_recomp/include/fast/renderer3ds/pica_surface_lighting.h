#pragma once

#include "fast/renderer3ds/pica_light_binding.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_environment_lighting.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

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

// Point lights with a shadow cube map this sample: atlas row i belongs to
// Identities[i] (zero for an empty row).
struct PicaPointShadowAssignment {
    std::array<uint64_t, 8> Identities{};
    // range (fraction of the radius), strength, face texels, border texels
    std::array<float, 4> Parameters{};
    // 1/width, 1/height, near-plane fraction of the far plane, enabled
    std::array<float, 4> Atlas{};
};

struct alignas(16) PicaSurfaceLightingUniforms {
    std::array<float, 4> MaterialDiffuse{};
    std::array<float, 4> MaterialAmbient{};
    // Material-weighted ambient with the environment policy already applied.
    std::array<float, 4> ResolvedAmbient{};
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
};

static_assert(sizeof(PicaSurfaceLightingUniforms) == 1488U);

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
    const PicaPointShadowAssignment* pointShadows = nullptr) noexcept;

// Removes the scene directional lights a draw's native per-pixel (fragment)
// lighting already applies: those with the direction (view space, toward
// the light) of one of its native directional lights. They are not added
// again; a shadow covering one applies to the native light instead
// (PicaSurfaceLightingBody, receiver slots 0-7 = native lights). Returns how
// many were removed.
uint32_t ExcludePicaSceneDirectionalsLitNatively(
    PicaSurfaceLightingUniforms& uniforms,
    std::span<const std::array<float, 3>> nativeDirectionalViewDirections) noexcept;

[[nodiscard]] std::string_view PicaSurfaceLightingDeclarations() noexcept;
[[nodiscard]] std::string_view PicaSurfaceLightingBody() noexcept;

} // namespace Fast::Renderer3ds
