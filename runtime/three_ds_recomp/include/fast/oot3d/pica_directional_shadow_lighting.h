#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/oot3d/pica_scene_semantics.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

inline constexpr uint32_t kPicaDirectionalShadowTextureBinding = 10U;
inline constexpr uint32_t kPicaDirectionalShadowSamplerBinding = 11U;
inline constexpr uint32_t kPicaDirectionalShadowUniformBinding = 12U;

enum class PicaDirectionalShadowLightingEligibility : uint8_t {
    Disabled,
    UnsupportedShader,
    VertexPrimary,
    FragmentPrimary,
};

struct alignas(16) PicaDirectionalShadowReceiverUniforms {
    std::array<float, 16> ViewToShadowTexture{};
    // strength, inverse map resolution, reserved, depth bias
    std::array<float, 4> ShadowState{};
    // Per-channel fraction of native direct lighting cast by the selected
    // world-space light cluster.
    std::array<float, 4> ShadowedDirectFraction{};
    // shadow-map width, shadow-map height, PCF radius, flags
    std::array<uint32_t, 4> Extent{};
};

static_assert(sizeof(PicaDirectionalShadowReceiverUniforms) == 112U);

enum class PicaDirectionalShadowReceiverContribution : uint8_t {
    None,
    BakedRigidMaterial,
};

struct PicaDirectionalShadowReceiverBinding {
    PicaDirectionalShadowReceiverUniforms Uniforms;
    PicaDirectionalShadowReceiverClass Classification =
        PicaDirectionalShadowReceiverClass::LightingUnavailable;
    PicaDirectionalShadowReceiverContribution Contribution =
        PicaDirectionalShadowReceiverContribution::None;

    [[nodiscard]] bool Bound() const noexcept {
        return (Uniforms.Extent[3] & 1U) != 0U;
    }
};

struct PicaDirectionalShadowHistoryState {
    DirectionalShadowMatrix WorldToShadowTexture{};
    DirectionalShadowVector WorldLightDirectionTowardSource{};
    uint64_t ProducedFrameId = 0U;
    uint32_t Resolution = 0U;
    float Strength = 0.0F;
    float DepthBias = 0.0F;
    uint8_t PcfRadius = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return ProducedFrameId != 0U && Resolution != 0U;
    }
};

struct PicaDirectionalShadowLightingInstrumentation {
    std::string Declarations;
    std::string Body;
    uint64_t FragmentKey = 0U;
    ::Oot3d::Renderer::PicaShaderHook InsertionHook =
        ::Oot3d::Renderer::PicaShaderHook::PicaLighting;
    PicaDirectionalShadowLightingEligibility Eligibility =
        PicaDirectionalShadowLightingEligibility::Disabled;

    [[nodiscard]] bool Applied() const noexcept {
        return Eligibility ==
                   PicaDirectionalShadowLightingEligibility::VertexPrimary ||
               Eligibility ==
                   PicaDirectionalShadowLightingEligibility::FragmentPrimary;
    }
};

[[nodiscard]] PicaDirectionalShadowLightingInstrumentation
BuildPicaDirectionalShadowLightingInstrumentation(
    uint64_t originalFragmentKey, bool enabled,
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks);

[[nodiscard]] PicaDirectionalShadowReceiverBinding
BuildPicaDirectionalShadowReceiverBinding(
    const DirectionalShadowMatrix& currentViewToWorld,
    const PicaNativeLightingState& nativeLighting,
    bool skinnedGeometry,
    const PicaDirectionalShadowHistoryState& history) noexcept;

} // namespace Fast::Oot3d
