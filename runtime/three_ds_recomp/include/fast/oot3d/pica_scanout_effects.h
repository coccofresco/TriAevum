#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/scene_view_runtime.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Fast::Oot3d {

struct PicaScanoutPushConstants {
    uint32_t FlipY = 0;
    uint32_t AaMode = 0;
    uint32_t Cacao = 0;
    uint32_t Outline = 0;
    float InvWidth = 1.0F;
    float InvHeight = 1.0F;
    float OutlineWidth = 1.0F;
    float OutlineDepthSensitivity = 1.0F;
    float OutlineColor[3] = { 0.03F, 0.04F, 0.06F };
    float OutlineOpacity = 0.85F;
    float OutlineNormalSensitivity = 1.0F;
    float ReflectionStrength = 0.65F;
    float ReflectionMaxDistance = 1500.0F;
    float ReflectionThickness = 8.0F;
    float ReflectionEdgeFade = 0.08F;
    uint32_t Reflections = 0;
    uint32_t ReflectionMaxSteps = 40;
    uint32_t HiZMipCount = 1;
    uint32_t ReflectionDebug = 0;
    float ProjectionScaleX = 1.0F;
    float ProjectionScaleY = 1.0F;
    float ProjectionOffsetX = 0.0F;
    float ProjectionOffsetY = 0.0F;
    float NearPlane = 0.1F;
    float FarPlane = 1000.0F;
    float ReflectionRoughnessBias = 0.0F;
    float ReflectionNormalReject = 0.0F;
    uint32_t InputLinear = 0;
    uint32_t EncodeSrgb = 0;
    float OutlineSoftness = 1.0F;
};

static_assert(sizeof(PicaScanoutPushConstants) == 128U);

struct PicaScanoutPolicyInput {
    uint32_t TransferFlags = 0;
    uint32_t AaMode = 0;
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t HiZMipCount = 1;
    bool CacaoAvailable = false;
    bool OutlineAvailable = false;
    bool ReflectionsAvailable = false;
    bool TemporalOutput = false;
    bool EffectsComposited = false;
    SceneColorEncoding InputEncoding = SceneColorEncoding::Unknown;
    bool TargetSrgb = false;
    EffectsSettings Effects;
    std::optional<PerspectiveViewState> Perspective;
};

[[nodiscard]] bool ValidatePicaScanoutPolicyInput(
    const PicaScanoutPolicyInput& input) noexcept;
[[nodiscard]] PicaScanoutPushConstants BuildPicaScanoutPushConstants(
    const PicaScanoutPolicyInput& input);
[[nodiscard]] std::string BuildPicaScanoutVertexShader();
[[nodiscard]] std::string BuildPicaScanoutFragmentShader(
    bool separateSampler = false,
    std::optional<int> diagnosticMode = std::nullopt);

} // namespace Fast::Oot3d
