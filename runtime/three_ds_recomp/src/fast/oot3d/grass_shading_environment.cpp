#include "fast/oot3d/grass_shading_environment.h"

#include "fast/oot3d/directional_shadows.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

std::array<float, 3> Normalize(std::array<float, 3> value) {
    const float lengthSquared =
        value[0] * value[0] + value[1] * value[1] +
        value[2] * value[2];
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 1.0e-12F) {
        return {};
    }
    if (std::abs(lengthSquared - 1.0F) <= 1.0e-5F) {
        return value;
    }
    const float length = std::sqrt(lengthSquared);
    for (float& component : value) {
        component /= length;
    }
    return value;
}

float Dot(const std::array<float, 3>& left, const std::array<float, 3>& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

} // namespace

GrassShadingEnvironment DecodeGrassShadingEnvironment(std::span<const uint8_t> packedVertexUniforms,
                                                      std::span<const uint8_t> packedFragmentUniforms,
                                                      const ::Fast::Renderer3ds::PicaFragmentFeatureView& fragmentFeatures,
                                                      const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view) noexcept {
    GrassShadingEnvironment result;
    const auto native = DecodePicaNativeDrawEnvironment(
        packedVertexUniforms, packedFragmentUniforms, fragmentFeatures);
    result.Depth = {
        native.Depth.Scale, native.Depth.Offset,
        native.Depth.WBuffering, native.Depth.Valid};

    if (native.Lighting.Available && native.Lighting.Enabled) {
        for (size_t lightIndex = 0U;
             lightIndex < native.Lighting.ActiveLightCount; ++lightIndex) {
            const auto& nativeLight = native.Lighting.Lights[lightIndex];
            const auto worldTowardSource = TransformPicaLightDirectionToWorld(
                nativeLight.DirectionViewTowardSource, view.Eye, view.At);
            if (worldTowardSource == std::array<float, 3>{}) {
                continue;
            }
            auto& light = result.Lights[result.ActiveLightCount];
            light.DirectionWorldTowardSource = worldTowardSource;
            for (size_t channel = 0U; channel < 3U; ++channel) {
                light.Diffuse[channel] = std::clamp(
                    nativeLight.Diffuse[channel], 0.0F, 1.0F);
                light.Ambient[channel] = std::clamp(
                    nativeLight.Ambient[channel], 0.0F, 1.0F);
            }
            light.Valid = true;
            ++result.ActiveLightCount;
        }
    }

    if (native.Fog.Available && native.Fog.Enabled &&
        DecodePicaNativeFogLut(
            packedFragmentUniforms, native.Fog, result.Fog.Lut)) {
        for (size_t channel = 0U; channel < 3U; ++channel) {
            result.Fog.Color[channel] = std::clamp(
                native.Fog.Color[channel], 0.0F, 1.0F);
        }
        result.Fog.Flip = native.Fog.Flip;
        result.Fog.Enabled = true;
    }
    return result;
}

GrassLightingResult EvaluateGrassLighting(const GrassShadingEnvironment& environment,
                                          const std::array<float, 3>& worldNormal) noexcept {
    GrassLightingResult result;
    const auto normal = Normalize(worldNormal);
    if (environment.ActiveLightCount == 0U || normal == std::array<float, 3>{}) {
        return result;
    }

    std::array<float, 3> ambient{};
    std::array<float, 3> direct{};
    for (size_t lightIndex = 0U; lightIndex < environment.ActiveLightCount; ++lightIndex) {
        const auto& light = environment.Lights[lightIndex];
        if (!light.Valid) {
            continue;
        }
        // Generated grass is two-sided. Absolute N.L preserves the native
        // CMB light direction while avoiding an arbitrary dark side on
        // crossed blade planes.
        const float diffuseFactor = std::abs(std::clamp(Dot(normal, light.DirectionWorldTowardSource), -1.0F, 1.0F));
        for (size_t channel = 0U; channel < 3U; ++channel) {
            ambient[channel] += light.Ambient[channel];
            direct[channel] += light.Diffuse[channel] * diffuseFactor;
        }
    }
    for (size_t channel = 0U; channel < 3U; ++channel) {
        const float total = ambient[channel] + direct[channel];
        result.Rgb[channel] = std::clamp(total, 0.0F, 1.0F);
        result.AmbientResponse[channel] = total > 1.0e-6F ? std::clamp(ambient[channel] / total, 0.0F, 1.0F) : 1.0F;
    }
    result.NativeLighting = true;
    return result;
}

float EvaluateGrassPicaDepth(const GrassPicaDepthState& depth, const std::array<float, 4>& picaClipPosition) noexcept {
    if (!depth.Valid || !std::isfinite(picaClipPosition[2]) || !std::isfinite(picaClipPosition[3]) ||
        std::abs(picaClipPosition[3]) <= 1.0e-7F) {
        return 0.0F;
    }
    float result = picaClipPosition[2] / picaClipPosition[3] * depth.Scale + depth.Offset;
    if (depth.WBuffering) {
        result *= picaClipPosition[3];
    }
    return std::clamp(result, 0.0F, 1.0F);
}

float SampleGrassPicaFog(const GrassPicaFogState& fog, float picaDepth) noexcept {
    if (!fog.Enabled || !std::isfinite(picaDepth)) {
        return 1.0F;
    }
    const float fogIndex = (fog.Flip ? 1.0F - picaDepth : picaDepth) * static_cast<float>(kGrassFogLutEntryCount);
    const float floorIndex = std::clamp(std::floor(fogIndex), 0.0F, static_cast<float>(kGrassFogLutEntryCount - 1U));
    const size_t entry = static_cast<size_t>(floorIndex);
    return std::clamp(fog.Lut[entry][0] + fog.Lut[entry][1] * (fogIndex - floorIndex), 0.0F, 1.0F);
}

} // namespace Fast::Oot3d
