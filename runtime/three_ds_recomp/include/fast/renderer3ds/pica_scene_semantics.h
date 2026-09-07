#pragma once

#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_shader_hooks.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Renderer3ds {

using PicaNativeMatrix4 = std::array<float, 16>;

inline constexpr size_t kPicaNativeLightCount = 3U;
inline constexpr size_t kPicaNativeFogLutEntryCount = 128U;

struct PicaNativeLightState {
    std::array<float, 3> DirectionViewTowardSource{};
    std::array<float, 3> Diffuse{};
    std::array<float, 3> Ambient{};
    bool Valid = false;
};

struct PicaNativeLightingState {
    std::array<PicaNativeLightState, kPicaNativeLightCount> Lights{};
    uint8_t ActiveLightCount = 0U;
    bool Enabled = false;
    bool Available = false;
};

struct PicaNativeFragmentLightState {
    std::array<float, 3> Specular0{};
    std::array<float, 3> Specular1{};
    std::array<float, 3> Diffuse{};
    std::array<float, 3> Ambient{};
    std::array<float, 3> PositionOrDirectionView{};
    std::array<float, 3> SpotDirectionView{};
    float DistanceAttenuationBias = 0.0F;
    float DistanceAttenuationScale = 0.0F;
    bool Active = false;
    bool ValuesAvailable = false;
};

struct PicaNativeFragmentLightingState {
    PicaFragmentLightingLayout Layout;
    std::array<PicaNativeFragmentLightState, kPicaFragmentLightCount> Lights{};
    std::array<float, 3> GlobalAmbient{};
    bool Enabled = false;
    bool Available = false;

    [[nodiscard]] const PicaNativeFragmentLightState*
    EvaluationLight(size_t slot) const noexcept {
        if (!Available || slot >= Layout.ActiveLightCount) {
            return nullptr;
        }
        const uint8_t nativeIndex = Layout.LightPermutation[slot];
        return nativeIndex < Lights.size() &&
                       Lights[nativeIndex].ValuesAvailable
                   ? &Lights[nativeIndex]
                   : nullptr;
    }
};

struct PicaNativeDepthState {
    float Scale = 1.0F;
    float Offset = 0.0F;
    bool WBuffering = false;
    bool Valid = false;
};

struct PicaNativeFogState {
    std::array<float, 3> Color{};
    uint64_t LutContentVersion = 0U;
    uint16_t LutEntryCount = 0U;
    uint8_t Mode = 0U;
    bool Enabled = false;
    bool Flip = false;
    bool Available = false;
};

struct PicaNativeTransformState {
    PicaVertexTransformLayout Layout;
    PicaNativeMatrix4 CurrentViewToWorld{};
    PicaNativeMatrix4 PreviousViewToWorld{};
    PicaNativeMatrix4 CurrentClipToWorld{};
    PicaNativeMatrix4 PreviousClipToWorld{};
    bool ProgramAvailable = false;
    bool CurrentAvailable = false;
    bool PreviousAvailable = false;
    bool CurrentViewToWorldAvailable = false;
    bool PreviousViewToWorldAvailable = false;
    bool CurrentClipToWorldAvailable = false;
    bool PreviousClipToWorldAvailable = false;
    bool CurrentUsesSkeleton = false;
    bool PreviousUsesSkeleton = false;
};

struct PicaNativeSkeletonState {
    PicaVertexSkeletonLayout Layout;
    uint8_t CurrentInfluenceCount = 0U;
    uint8_t PreviousInfluenceCount = 0U;
    bool ProgramAvailable = false;
    bool CurrentAvailable = false;
    bool PreviousAvailable = false;
};

struct PicaNativeVertexState {
    PicaNativeTransformState Transform;
    PicaNativeSkeletonState Skeleton;
};

struct PicaNativeDrawEnvironment {
    PicaNativeLightingState Lighting;
    PicaNativeFragmentLightingState FragmentLighting;
    PicaNativeDepthState Depth;
    PicaNativeFogState Fog;
    bool FragmentLightingEnabled = false;
};

} // namespace Fast::Renderer3ds
