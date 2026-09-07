#pragma once

#include "fast/oot3d/pica_scene_semantics.h"
#include "fast/renderer3ds/pica_scene_payloads.h"

#include <array>
#include <cstdint>
#include <span>

namespace Fast::Oot3d {

inline constexpr size_t kGrassNativeLightCount = kPicaNativeLightCount;
inline constexpr size_t kGrassFogLutEntryCount =
    kPicaNativeFogLutEntryCount;

struct GrassNativeLight {
    std::array<float, 3> DirectionWorldTowardSource{};
    std::array<float, 3> Diffuse{};
    std::array<float, 3> Ambient{};
    bool Valid = false;
};

struct GrassPicaDepthState {
    float Scale = 1.0F;
    float Offset = 0.0F;
    bool WBuffering = false;
    bool Valid = false;
};

struct GrassPicaFogState {
    std::array<float, 3> Color{};
    std::array<std::array<float, 2>, kGrassFogLutEntryCount> Lut{};
    bool Enabled = false;
    bool Flip = false;
};

struct GrassShadingEnvironment {
    std::array<GrassNativeLight, kGrassNativeLightCount> Lights{};
    GrassPicaDepthState Depth;
    GrassPicaFogState Fog;
    uint8_t ActiveLightCount = 0;
};

struct GrassLightingResult {
    std::array<float, 3> Rgb{ 1.0F, 1.0F, 1.0F };
    std::array<float, 3> AmbientResponse{ 1.0F, 1.0F, 1.0F };
    bool NativeLighting = false;
};

[[nodiscard]] GrassShadingEnvironment DecodeGrassShadingEnvironment(std::span<const uint8_t> packedVertexUniforms,
                                                                    std::span<const uint8_t> packedFragmentUniforms,
                                                                    const ::Fast::Renderer3ds::PicaFragmentFeatureView& fragmentFeatures,
                                                                    const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view) noexcept;

[[nodiscard]] GrassLightingResult EvaluateGrassLighting(const GrassShadingEnvironment& environment,
                                                        const std::array<float, 3>& worldNormal) noexcept;

[[nodiscard]] float EvaluateGrassPicaDepth(const GrassPicaDepthState& depth,
                                           const std::array<float, 4>& picaClipPosition) noexcept;

[[nodiscard]] float SampleGrassPicaFog(const GrassPicaFogState& fog, float picaDepth) noexcept;

} // namespace Fast::Oot3d
