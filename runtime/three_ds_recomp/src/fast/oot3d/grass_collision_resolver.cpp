#include "fast/oot3d/grass_collision_resolver.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

float Length2(float x, float z) { return std::sqrt(x * x + z * z); }

} // namespace

GrassCollisionResult ResolveGrassCollision(
    const std::array<float, 3>& bladeBase, float bladeHeight,
    const GrassInteractor& interactor,
    const InteractiveGrassSettings& settings) {
    GrassCollisionResult result;
    if (settings.CollisionPush <= 0.0F || bladeHeight <= 0.0F) return result;

    const float segmentX = interactor.Position[0] -
                           interactor.PreviousPosition[0];
    const float segmentZ = interactor.Position[2] -
                           interactor.PreviousPosition[2];
    const float segmentLengthSquared = segmentX * segmentX + segmentZ * segmentZ;
    float t = 1.0F;
    if (!interactor.Teleported && segmentLengthSquared > 1.0e-6F) {
        t = std::clamp(
            ((bladeBase[0] - interactor.PreviousPosition[0]) * segmentX +
             (bladeBase[2] - interactor.PreviousPosition[2]) * segmentZ) /
                segmentLengthSquared,
            0.0F, 1.0F);
    }
    const float closestX = interactor.PreviousPosition[0] + segmentX * t;
    const float closestY = interactor.PreviousPosition[1] +
                           (interactor.Position[1] -
                            interactor.PreviousPosition[1]) * t;
    const float closestZ = interactor.PreviousPosition[2] + segmentZ * t;
    const float radius = std::max(
        0.01F, interactor.Radius * settings.ColliderRadiusMultiplier);
    float dx = bladeBase[0] - closestX;
    float dz = bladeBase[2] - closestZ;
    const float distance = Length2(dx, dz);
    if (distance >= radius) return result;

    // Runtime samples expose Link's actor base. Treat the configured
    // half-height as half of a standing capsule while keeping its lower end at
    // that base; this also rejects foliage on bridges and stacked floors.
    const float fullHeight = std::max(
        radius, interactor.HalfHeight * 2.0F *
                    settings.ColliderHeightMultiplier);
    const float margin = settings.InteractionVerticalMargin;
    const float colliderBottom = closestY - margin;
    const float colliderTop = closestY + fullHeight + margin;
    const float bladeBottom = bladeBase[1];
    const float bladeTop = bladeBase[1] + bladeHeight;
    result.VerticalOverlap =
        bladeTop >= colliderBottom && bladeBottom <= colliderTop;
    if (!result.VerticalOverlap) return result;

    if (distance > 1.0e-5F) {
        dx /= distance;
        dz /= distance;
    } else {
        const float velocityLength =
            Length2(interactor.Velocity[0], interactor.Velocity[2]);
        const float motionLength = std::sqrt(segmentLengthSquared);
        if (velocityLength > 1.0e-5F) {
            dx = interactor.Velocity[0] / velocityLength;
            dz = interactor.Velocity[2] / velocityLength;
        } else if (motionLength > 1.0e-5F) {
            dx = segmentX / motionLength;
            dz = segmentZ / motionLength;
        } else {
            dx = 1.0F;
            dz = 0.0F;
        }
    }

    const float speed = Length2(interactor.Velocity[0],
                                interactor.Velocity[2]);
    const float velocityGain = 1.0F +
        std::clamp(speed / 300.0F, 0.0F, 2.0F) *
            settings.CollisionVelocityResponse;
    result.Weight = (1.0F - distance / radius) *
                    settings.CollisionPush * velocityGain;
    const float bend = std::min(result.Weight, settings.MaximumBend);
    result.Bend = {dx * bend, dz * bend};
    return result;
}

} // namespace Fast::Oot3d
