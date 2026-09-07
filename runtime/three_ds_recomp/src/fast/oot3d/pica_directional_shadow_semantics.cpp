#include "fast/oot3d/pica_directional_shadow_semantics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr float kMinimumLightEnergy = 1.0e-6F;
constexpr float kDirectionClusterCosine = 0.999F;

float Luminance(const std::array<float, 3>& color) noexcept {
    return 0.2126F * std::max(color[0], 0.0F) +
           0.7152F * std::max(color[1], 0.0F) +
           0.0722F * std::max(color[2], 0.0F);
}

float Dot(const DirectionalShadowVector& left,
          const DirectionalShadowVector& right) noexcept {
    return left[0] * right[0] + left[1] * right[1] +
           left[2] * right[2];
}

bool Normalize(DirectionalShadowVector& value) noexcept {
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumLightEnergy) {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    for (float& component : value) {
        component *= inverseLength;
    }
    return true;
}

struct LightCluster {
    DirectionalShadowVector WeightedDirection{};
    DirectionalShadowVector Direction{};
    const PicaSceneDrawRecord* ReferenceDraw = nullptr;
    uint32_t CandidateCount = 0U;
    uint8_t ReferenceLightIndex = 0U;
    float ReferenceWeight = 0.0F;
    float Weight = 0.0F;
};

} // namespace

PicaDirectionalShadowLightSelection
ResolvePicaDirectionalShadowLightSelection(
    std::span<const PicaSceneDrawRecord* const> draws) noexcept {
    std::vector<LightCluster> clusters;
    uint32_t candidateCount = 0U;
    for (const PicaSceneDrawRecord* draw : draws) {
        if (draw == nullptr || !draw->NativeLighting.Available ||
            !draw->NativeLighting.Enabled ||
            !draw->NativeTransform.CurrentViewToWorldAvailable) {
            continue;
        }
        const float geometryWeight = static_cast<float>(
            std::max(draw->VertexOrIndexCount, 1U));
        for (uint8_t lightIndex = 0U;
             lightIndex < draw->NativeLighting.ActiveLightCount;
             ++lightIndex) {
            const auto& light = draw->NativeLighting.Lights[lightIndex];
            const float lightEnergy = Luminance(light.Diffuse);
            if (!light.Valid || !std::isfinite(lightEnergy) ||
                lightEnergy <= kMinimumLightEnergy) {
                continue;
            }
            DirectionalShadowVector worldDirection =
                TransformPicaLightDirectionToWorld(
                    light.DirectionViewTowardSource,
                    draw->NativeTransform.CurrentViewToWorld);
            if (!Normalize(worldDirection)) {
                continue;
            }
            ++candidateCount;
            const float weight = geometryWeight * lightEnergy;
            auto cluster = std::find_if(
                clusters.begin(), clusters.end(),
                [&](const LightCluster& value) {
                    return Dot(value.Direction, worldDirection) >=
                           kDirectionClusterCosine;
                });
            if (cluster == clusters.end()) {
                clusters.push_back({});
                cluster = std::prev(clusters.end());
                cluster->Direction = worldDirection;
            }
            for (size_t component = 0U; component < 3U; ++component) {
                cluster->WeightedDirection[component] +=
                    worldDirection[component] * weight;
            }
            cluster->Weight += weight;
            ++cluster->CandidateCount;
            if (weight > cluster->ReferenceWeight) {
                cluster->ReferenceWeight = weight;
                cluster->ReferenceDraw = draw;
                cluster->ReferenceLightIndex = lightIndex;
            }
            cluster->Direction = cluster->WeightedDirection;
            Normalize(cluster->Direction);
        }
    }

    const auto dominant = std::max_element(
        clusters.begin(), clusters.end(),
        [](const LightCluster& left, const LightCluster& right) {
            return left.Weight < right.Weight;
        });
    if (dominant == clusters.end() || dominant->ReferenceDraw == nullptr ||
        dominant->Weight <= kMinimumLightEnergy) {
        return {};
    }
    return {
        dominant->Direction,
        dominant->ReferenceDraw,
        candidateCount,
        static_cast<uint32_t>(clusters.size()),
        dominant->ReferenceLightIndex,
        dominant->Weight,
    };
}

PicaDirectionalShadowReceiverLight
ResolvePicaDirectionalShadowReceiverLight(
    const PicaNativeLightingState& lighting,
    const DirectionalShadowMatrix& viewToWorld,
    const DirectionalShadowVector& worldDirectionTowardSource) noexcept {
    PicaDirectionalShadowReceiverLight result;
    if (!lighting.Available) {
        return result;
    }
    if (!lighting.Enabled) {
        result.Classification =
            PicaDirectionalShadowReceiverClass::LightingDisabled;
        return result;
    }

    DirectionalShadowVector targetDirection = worldDirectionTowardSource;
    if (!Normalize(targetDirection)) {
        result.Classification =
            PicaDirectionalShadowReceiverClass::DirectUnmatched;
        return result;
    }

    std::array<float, 3> totalAmbient{};
    std::array<float, 3> totalDirect{};
    std::array<float, 3> shadowedDirect{};
    uint8_t directLightCount = 0U;
    for (uint8_t lightIndex = 0U;
         lightIndex < lighting.ActiveLightCount; ++lightIndex) {
        const auto& light = lighting.Lights[lightIndex];
        if (!light.Valid) {
            continue;
        }
        for (size_t channel = 0U; channel < 3U; ++channel) {
            totalAmbient[channel] += std::max(light.Ambient[channel], 0.0F);
            totalDirect[channel] += std::max(light.Diffuse[channel], 0.0F);
        }
        if (Luminance(light.Diffuse) <= kMinimumLightEnergy) {
            continue;
        }
        ++directLightCount;
        DirectionalShadowVector worldDirection =
            TransformPicaLightDirectionToWorld(
                light.DirectionViewTowardSource, viewToWorld);
        if (!Normalize(worldDirection) ||
            Dot(worldDirection, targetDirection) <
                kDirectionClusterCosine) {
            continue;
        }
        ++result.MatchedLightCount;
        for (size_t channel = 0U; channel < 3U; ++channel) {
            shadowedDirect[channel] +=
                std::max(light.Diffuse[channel], 0.0F);
        }
    }
    if (directLightCount == 0U) {
        result.Classification =
            PicaDirectionalShadowReceiverClass::AmbientOnly;
        return result;
    }
    if (result.MatchedLightCount == 0U) {
        result.Classification =
            PicaDirectionalShadowReceiverClass::DirectUnmatched;
        return result;
    }
    for (size_t channel = 0U; channel < 3U; ++channel) {
        const float total = totalAmbient[channel] + totalDirect[channel];
        result.ShadowedDirectFraction[channel] =
            total > kMinimumLightEnergy
                ? std::clamp(shadowedDirect[channel] / total, 0.0F, 1.0F)
                : 0.0F;
    }
    result.Classification =
        Luminance(result.ShadowedDirectFraction) > kMinimumLightEnergy
            ? PicaDirectionalShadowReceiverClass::DirectMatched
            : PicaDirectionalShadowReceiverClass::AmbientOnly;
    return result;
}

} // namespace Fast::Oot3d
