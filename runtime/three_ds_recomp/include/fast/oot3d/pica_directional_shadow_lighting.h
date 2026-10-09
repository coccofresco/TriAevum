#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/oot3d/pica_scene_semantics.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <memory>
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

struct PicaDirectionalShadowReceiverLayout {
    std::array<float, 16> ViewToLight{};
    // Light-plane camera position and active clipmap level count.
    std::array<float, 4> CameraLight{};
    std::array<std::array<float, 4>, Renderer3ds::kVirtualShadowLevelCount> Levels{};
    // Packed as uvec4[] in std140, not scalar uint[].
    std::array<uint32_t, Renderer3ds::kVirtualShadowTableEntries> PageTable{};
};

static_assert(sizeof(PicaDirectionalShadowReceiverLayout) == 2800U);

inline constexpr uint32_t kPicaDirectionalShadowBoundFlag = 1U;
// The older keyframe of a moving light is displayed (dissolve).
inline constexpr uint32_t kPicaDirectionalShadowDissolveFlag = 2U;

struct alignas(16) PicaDirectionalShadowReceiverUniforms {
    // strength, inverse atlas width, maximum distance, finest-level depth bias
    std::array<float, 4> ShadowState{};
    // X: the smooth-shaded receivers' normal bias (texels); Y: 1 with contact
    // shadows; W: selected shader-light mask for surface lighting.
    std::array<float, 4> Receiver{};
    // layout resolution (twice), PCF radius, flags
    std::array<uint32_t, 4> Extent{};
    // weight of layout 0, inverse atlas height, atlas X offset of layouts 0 and 1
    std::array<float, 4> Dissolve{};
    // The receiver's view position to the contact shadows' clip space (read
    // while Receiver[1] is 1).
    std::array<float, 16> ContactProjection{};
    // Layout 0 is the newest; layout 1 is read only with the dissolve flag.
    std::array<PicaDirectionalShadowReceiverLayout, 2> Layouts{};

    // The bytes a receiver reads: without the dissolve, layout 1 is never
    // read and need not be uploaded.
    [[nodiscard]] size_t UsedBytes() const noexcept {
        return (Extent[3] & kPicaDirectionalShadowDissolveFlag) != 0U
            ? sizeof(*this) : sizeof(*this) - sizeof(PicaDirectionalShadowReceiverLayout);
    }
};

static_assert(sizeof(PicaDirectionalShadowReceiverUniforms) == 5728U);


struct PicaDirectionalShadowReceiverBinding {
    PicaDirectionalShadowReceiverUniforms Uniforms;
    PicaDirectionalShadowReceiverClass Classification =
        PicaDirectionalShadowReceiverClass::LightingUnavailable;

    [[nodiscard]] bool Bound() const noexcept {
        return (Uniforms.Extent[3] & kPicaDirectionalShadowBoundFlag) != 0U;
    }
};

// The older keyframe of a moving light (shadow keyframes).
struct PicaDirectionalShadowOlderLayout {
    Renderer3ds::VirtualShadowLayout Virtual;
    std::array<uint32_t, Renderer3ds::kVirtualShadowTableEntries> PageTable{};
    uint32_t Region = 0U;
};

struct PicaDirectionalShadowHistoryState {
    // The newest layout and its page table.
    Renderer3ds::VirtualShadowLayout Virtual;
    std::array<uint32_t, Renderer3ds::kVirtualShadowTableEntries> PageTable{};
    // Displayed with weight 1 - NewerWeight while a dissolve runs; shared, so
    // a still light's state stays as small as one layout.
    std::shared_ptr<const PicaDirectionalShadowOlderLayout> Older;
    float NewerWeight = 1.0F;
    // Atlas regions, side by side, each of the layout resolution.
    uint32_t AtlasRegions = 1U;
    uint32_t Region = 0U;
    // The light the receivers' lights are matched against: the scene's
    // current light, which the layouts follow.
    DirectionalShadowVector WorldLightDirectionTowardSource{};
    uint64_t ProducedFrameId = 0U;
    float Strength = 0.0F;
    float DepthBias = 0.0F;
    uint8_t PcfRadius = 0U;
    float NormalBias = 0.0F;
    // Contact shadows of this sample: their clip space from the world.
    bool ContactShadows = false;
    DirectionalShadowMatrix WorldToContactClip{};

    [[nodiscard]] bool Valid() const noexcept {
        return ProducedFrameId != 0U && Virtual.Valid();
    }
    [[nodiscard]] bool Dissolving() const noexcept {
        return NewerWeight < 1.0F && Older != nullptr && Older->Virtual.Valid();
    }
};

// The receiver declarations every directional-shadow lighting variant adds.
[[nodiscard]] std::string PicaDirectionalShadowLightingDeclarations();

struct PicaDirectionalShadowLightingInstrumentation {
    std::string Declarations;
    uint64_t FragmentKey = 0U;
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

[[nodiscard]] PicaDirectionalShadowReceiverUniforms PackPicaDirectionalShadowReceiverUniforms(
    const DirectionalShadowMatrix& viewToWorld, const PicaDirectionalShadowHistoryState& history) noexcept;
// In place: without a dissolve, layout 1 is left untouched.
void PackPicaDirectionalShadowReceiverUniforms(const DirectionalShadowMatrix& viewToWorld,
                                               const PicaDirectionalShadowHistoryState& history,
                                               PicaDirectionalShadowReceiverUniforms& uniforms) noexcept;


[[nodiscard]] PicaDirectionalShadowReceiverBinding
BuildPicaSurfaceShadowReceiverBinding(
    const DirectionalShadowMatrix& currentViewToWorld,
    std::span<const PicaSurfaceShadowLight> lights,
    const PicaDirectionalShadowHistoryState& history) noexcept;

} // namespace Fast::Oot3d
