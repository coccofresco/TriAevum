#include "oot3d_actor_visibility_policy.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Oot3dNativeGame {
namespace {

bool Finite(const ActorCullingSample& sample) noexcept {
    return std::isfinite(sample.X) && std::isfinite(sample.Y) &&
           std::isfinite(sample.Z) && std::isfinite(sample.W) &&
           std::isfinite(sample.Forward) && std::isfinite(sample.Scale) &&
           std::isfinite(sample.Downward);
}

float Factor(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 1.0F) : 1.0F;
}

ActorCullingClass Classify(const ActorCullingSample& sample, float forward,
                           float horizontal, float vertical) noexcept {
    if (!Finite(sample)) return ActorCullingClass::Outside;
    if (!(sample.Z < forward + sample.Scale)) return ActorCullingClass::Far;
    if (!(sample.Z > -sample.Scale)) return ActorCullingClass::Outside;
    // The title divides by W only from 1 up.
    const float q = sample.W < 1.0F ? 1.0F : 1.0F / sample.W;
    const bool inside = (std::abs(sample.X) - sample.Scale) * q < horizontal &&
                        (sample.Y + sample.Downward) * q > -vertical &&
                        (sample.Y - sample.Scale) * q < vertical;
    return inside ? ActorCullingClass::Inside : ActorCullingClass::Outside;
}

} // namespace

ActorCullingClass ClassifyNativeActorCulling(
    const ActorCullingSample& sample) noexcept {
    return Classify(sample, sample.Forward, 1.0F, 1.0F);
}

ActorCullingClass ClassifyExtendedActorCulling(
    const ActorCullingSample& sample,
    const ActorCullingVolume& volume) noexcept {
    const float multiplier = Factor(volume.DistanceMultiplier);
    float forward = sample.Forward;
    if (multiplier > 1.0F)
        forward = (forward + (forward < 500.0F ? 1000.0F : 0.0F)) * multiplier;
    forward = std::max(forward, sample.Forward);
    return Classify(sample, forward, Factor(volume.Horizontal), Factor(volume.Vertical));
}

float ObjMure2DistanceScalar(float nativeScalar, float projectedX,
                             float projectedZ, float horizontal,
                             bool extended) noexcept {
    if (!std::isfinite(nativeScalar)) return nativeScalar;
    float scalar = nativeScalar;
    if (extended)
        scalar = std::max(scalar, kExtendedActorDistanceMultiplier * kExtendedActorDistanceMultiplier);
    const float h = Factor(horizontal);
    if (h > 1.0F && std::isfinite(projectedX) && std::isfinite(projectedZ)) {
        // (X / h)^2 + Z^2 < T s'  <=>  X^2 + Z^2 < T s' (X^2 + Z^2) / ((X / h)^2 + Z^2).
        const float full = projectedX * projectedX + projectedZ * projectedZ;
        const float presented = (projectedX / h) * (projectedX / h) + projectedZ * projectedZ;
        if (presented > 1.0e-6F && std::isfinite(full)) scalar *= full / presented;
    }
    return scalar;
}

void SelectActorExtensions(std::span<ActorExtensionCandidate> candidates) {
    std::vector<ActorExtensionCandidate*> ordered;
    ordered.reserve(candidates.size());
    for (auto& candidate : candidates) {
        candidate.Selected = false;
        if (std::isfinite(candidate.DistanceSquared) && candidate.DistanceSquared >= 0.0F)
            ordered.push_back(&candidate);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        if (a->Retained != b->Retained) return a->Retained;
        if (a->DistanceSquared != b->DistanceSquared) return a->DistanceSquared < b->DistanceSquared;
        return a->Identity < b->Identity;
    });
    for (size_t i = 0; i < std::min<size_t>(ordered.size(), kActorExtensionLimit); ++i)
        ordered[i]->Selected = true;
}

bool ActorLightBudgetAllows(uint32_t used, uint32_t remaining, bool overflow) noexcept {
    const auto available = kActorLightBudget + (overflow ? kActorLightOverflowRecords : 0U) -
                           kActorLightAuxiliaryReserve;
    return used <= available && remaining <= available - used;
}

uint8_t NativeCulledActorFade(ActorCullingClass classification, uint8_t fade) noexcept {
    return classification == ActorCullingClass::Far && fade != 0U
        ? static_cast<uint8_t>(fade - 1U) : 0U;
}

} // namespace Oot3dNativeGame
