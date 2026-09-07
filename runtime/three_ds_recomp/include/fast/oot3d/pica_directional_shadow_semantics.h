#pragma once

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/pica_scene_frame.h"

#include <array>
#include <cstdint>
#include <span>

namespace Fast::Oot3d {

struct PicaDirectionalShadowLightSelection {
    DirectionalShadowVector WorldDirectionTowardSource{};
    const PicaSceneDrawRecord* ReferenceDraw = nullptr;
    uint32_t CandidateCount = 0U;
    uint32_t ClusterCount = 0U;
    uint8_t ReferenceLightIndex = 0U;
    float GeometryLightWeight = 0.0F;

    [[nodiscard]] bool Valid() const noexcept {
        return ReferenceDraw != nullptr && GeometryLightWeight > 0.0F;
    }
};

enum class PicaDirectionalShadowReceiverClass : uint8_t {
    LightingUnavailable,
    LightingDisabled,
    AmbientOnly,
    DirectUnmatched,
    DirectMatched,
};

struct PicaDirectionalShadowReceiverLight {
    std::array<float, 3> ShadowedDirectFraction{};
    uint8_t MatchedLightCount = 0U;
    PicaDirectionalShadowReceiverClass Classification =
        PicaDirectionalShadowReceiverClass::LightingUnavailable;

    [[nodiscard]] bool Valid() const noexcept {
        return Classification ==
               PicaDirectionalShadowReceiverClass::DirectMatched;
    }
};

[[nodiscard]] PicaDirectionalShadowLightSelection
ResolvePicaDirectionalShadowLightSelection(
    std::span<const PicaSceneDrawRecord* const> draws) noexcept;

[[nodiscard]] PicaDirectionalShadowReceiverLight
ResolvePicaDirectionalShadowReceiverLight(
    const PicaNativeLightingState& lighting,
    const DirectionalShadowMatrix& viewToWorld,
    const DirectionalShadowVector& worldDirectionTowardSource) noexcept;

} // namespace Fast::Oot3d
