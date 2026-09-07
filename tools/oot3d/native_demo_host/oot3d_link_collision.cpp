#include "oot3d_link_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "oot3d_demo_math.h"
#include "oot3d_native_collision_math.h"

namespace {

bool IsHorizontalCollisionWall(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    const double horizontalNormalLength =
        std::sqrt(static_cast<double>(poly.NormalX) * poly.NormalX +
                  static_cast<double>(poly.NormalZ) * poly.NormalZ);
    return std::abs(poly.NormalY) < kNativeWallNormalYThreshold && horizontalNormalLength > 1.0;
}

LinkCollisionSegmentXZ LongestPolygonSegmentXZ(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 vertices[] = {
        collision.Vertices[poly.VertexA],
        collision.Vertices[poly.VertexB],
        collision.Vertices[poly.VertexC],
    };

    LinkCollisionSegmentXZ best;
    double bestDistanceSq = 0.0;
    for (const auto pair : { std::pair<size_t, size_t>{ 0, 1 }, std::pair<size_t, size_t>{ 1, 2 },
                             std::pair<size_t, size_t>{ 2, 0 } }) {
        const double distanceSq = DistanceSqXZ(vertices[pair.first], vertices[pair.second]);
        if (distanceSq > bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = { vertices[pair.first], vertices[pair.second], true };
        }
    }
    if (bestDistanceSq <= 0.000001) {
        best.Valid = false;
    }
    return best;
}

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ClosestPointOnSegmentXZ(
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& point,
    const LinkCollisionSegmentXZ& segment,
    double* unclampedT = nullptr) {
    const double dx = segment.B.X - segment.A.X;
    const double dz = segment.B.Z - segment.A.Z;
    const double lengthSq = dx * dx + dz * dz;
    double t = 0.0;
    if (lengthSq > 0.000001) {
        t = ((point.X - segment.A.X) * dx + (point.Z - segment.A.Z) * dz) / lengthSq;
    }
    if (unclampedT != nullptr) {
        *unclampedT = t;
    }
    const double clampedT = std::clamp(t, 0.0, 1.0);
    return {
        segment.A.X + dx * clampedT,
        0.0,
        segment.A.Z + dz * clampedT,
    };
}

double DistancePointToSegmentSqXZ(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& point,
                                  const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& segmentA,
                                  const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& segmentB) {
    const double dx = segmentB.X - segmentA.X;
    const double dz = segmentB.Z - segmentA.Z;
    const double lengthSq = dx * dx + dz * dz;
    double t = 0.0;
    if (lengthSq > 0.000001) {
        t = ((point.X - segmentA.X) * dx + (point.Z - segmentA.Z) * dz) / lengthSq;
    }
    t = std::clamp(t, 0.0, 1.0);
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 closest = {
        segmentA.X + dx * t,
        0.0,
        segmentA.Z + dz * t,
    };
    return DistanceSqXZ(point, closest);
}

bool NormalizeXZ(ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value) {
    const double length = std::sqrt(value.X * value.X + value.Z * value.Z);
    if (length <= 0.000001) {
        return false;
    }
    value.X /= length;
    value.Y = 0.0;
    value.Z /= length;
    return true;
}

uint32_t NativeSurfaceWallFlags(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision, int polygonIndex,
    const LinkNativeCollisionActionConfig& config) {
    if (polygonIndex < 0 || static_cast<size_t>(polygonIndex) >= collision.Polygons.size()) {
        return 0;
    }
    const int surfaceIndex = collision.Polygons[static_cast<size_t>(polygonIndex)].Type;
    if (surfaceIndex < 0 || static_cast<size_t>(surfaceIndex) >= collision.SurfaceTypes.size()) {
        return 0;
    }
    const uint32_t data = collision.SurfaceTypes[static_cast<size_t>(surfaceIndex)].Data1;
    const uint32_t behavior =
        (data >> config.SurfaceBehaviorIndexShift) & config.SurfaceBehaviorIndexMask;
    return behavior < config.SurfaceWallFlags.size()
        ? config.SurfaceWallFlags[behavior]
        : 0;
}

bool FindNearestEntityCollisionSegmentHit(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
    const Vec3& from, const Vec3& to, bool checkOneFace,
    NativeCollisionSegmentHit& bestHit, int excludePolygon = -1) {
    bool found = false;
    bestHit = {};
    bestHit.DistanceSqFromStart = std::numeric_limits<double>::infinity();
    for (size_t polygonIndex = 0; polygonIndex < collision.Polygons.size(); ++polygonIndex) {
        const auto& poly = collision.Polygons[polygonIndex];
        if (static_cast<int>(polygonIndex) == excludePolygon || poly.IgnoreEntities) {
            continue;
        }
        NativeCollisionSegmentHit hit;
        if (!CollisionSegmentVsPolygon(collision, polygonIndex, from, to, checkOneFace, hit) ||
            (found && hit.DistanceSqFromStart >= bestHit.DistanceSqFromStart)) {
            continue;
        }
        bestHit = hit;
        found = true;
    }
    return found;
}

double AngleDifferenceS16(const Vec3& left, const Vec3& right) {
    const Vec3 leftXZ = Normalize({ left.X, 0.0, left.Z });
    const Vec3 rightXZ = Normalize({ right.X, 0.0, right.Z });
    if (Dot(leftXZ, leftXZ) <= 0.000001 || Dot(rightXZ, rightXZ) <= 0.000001) {
        return 32768.0;
    }
    const double radians = std::acos(std::clamp(Dot(leftXZ, rightXZ), -1.0, 1.0));
    return radians / kOot3dS16AngleToRadians;
}

bool CollisionPolygonsShareEdge(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& left,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& right) {
    const int leftVertices[] = { left.VertexA, left.VertexB, left.VertexC };
    const int rightVertices[] = { right.VertexA, right.VertexB, right.VertexC };
    int shared = 0;
    for (const int leftVertex : leftVertices) {
        for (const int rightVertex : rightVertices) {
            if (leftVertex == rightVertex) {
                ++shared;
                break;
            }
        }
    }
    return shared >= 2;
}

bool CollisionPolygonsShareClimbPlane(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& left,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& right) {
    return left.Type == right.Type &&
           std::abs(left.NormalX - right.NormalX) <= 1 &&
           std::abs(left.NormalY - right.NormalY) <= 1 &&
           std::abs(left.NormalZ - right.NormalZ) <= 1 &&
           std::abs(left.Dist - right.Dist) <= 1;
}

} // namespace

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ActorPlusDelta(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actor,
                                          const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& delta) {
    return { actor.X + delta.X, actor.Y + delta.Y, actor.Z + delta.Z };
}

void ResetLinkHorizontalCollisionState(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                       const LinkNativeLocomotionConfig& config, LinkInstance& link) {
    link.NativeHorizontalCollision = scene.Collision.Valid;
    link.HorizontalCollision = false;
    link.WallPolygonIndex = -1;
    link.HorizontalCollisionIterationCount = 0;
    link.WallPushNormalX = 0.0;
    link.WallPushNormalZ = 0.0;
    link.NativeWallSpeedLimitUnitsPerTick = config.RunSpeedUnitsPerTick;
    link.NativeWallSpeedScale = 1.0;
    link.NativeWorldYawToWallS16 = 0.0;
    link.NativeWallSpeedLimitActive = false;
}

bool FindLinkHorizontalCollision(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
                                 const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& previousActor,
                                 const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actor,
                                 LinkWallCollisionHit& bestHit) {
    bestHit = {};
    if (!scene.Collision.Valid || link.ColliderRadius <= 0.0) {
        return false;
    }

    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 movement = {
        actor.X - previousActor.X,
        actor.Y - previousActor.Y,
        actor.Z - previousActor.Z,
    };
    const double wallCheckY = actor.Y + link.NativeWallCheckHeight;
    double bestScore = -std::numeric_limits<double>::infinity();

    for (size_t polyIndex = 0; polyIndex < scene.Collision.Polygons.size(); ++polyIndex) {
        const auto& poly = scene.Collision.Polygons[polyIndex];
        if (poly.IgnoreEntities || !CollisionPolygonIndicesValid(scene.Collision, poly) ||
            !IsHorizontalCollisionWall(poly)) {
            continue;
        }
        if (wallCheckY < PolygonYMin(scene.Collision, poly) || wallCheckY > PolygonYMax(scene.Collision, poly)) {
            continue;
        }

        const auto segment = LongestPolygonSegmentXZ(scene.Collision, poly);
        if (!segment.Valid) {
            continue;
        }

        const auto previousClosest = ClosestPointOnSegmentXZ(previousActor, segment);
        double currentT = 0.0;
        auto currentClosest = ClosestPointOnSegmentXZ(actor, segment, &currentT);
        bool nearSegment = currentT >= 0.0 && currentT <= 1.0;
        bool endpointSweep = false;
        if (!nearSegment) {
            const double radiusSq = link.ColliderRadius * link.ColliderRadius;
            for (const auto& endpoint : { segment.A, segment.B }) {
                if (DistancePointToSegmentSqXZ(endpoint, previousActor, actor) <= radiusSq) {
                    currentClosest = endpoint;
                    nearSegment = true;
                    endpointSweep = true;
                    break;
                }
            }
        }
        if (!nearSegment) {
            continue;
        }

        ThreeDsRecomp::Oot3d::Oot3dDemoVec3 pushNormal = {
            previousActor.X - (endpointSweep ? currentClosest.X : previousClosest.X),
            0.0,
            previousActor.Z - (endpointSweep ? currentClosest.Z : previousClosest.Z),
        };
        if (!NormalizeXZ(pushNormal)) {
            pushNormal = { actor.X - currentClosest.X, 0.0, actor.Z - currentClosest.Z };
        }
        if (!NormalizeXZ(pushNormal)) {
            pushNormal = { static_cast<double>(poly.NormalX), 0.0, static_cast<double>(poly.NormalZ) };
        }
        if (!NormalizeXZ(pushNormal)) {
            continue;
        }

        const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 currentDelta = {
            actor.X - currentClosest.X,
            0.0,
            actor.Z - currentClosest.Z,
        };
        const double currentSigned = currentDelta.X * pushNormal.X + currentDelta.Z * pushNormal.Z;
        const double penetration = link.ColliderRadius - currentSigned;
        if (penetration <= 0.0 && !endpointSweep) {
            continue;
        }

        const double movementAlongPushNormal =
            movement.X * pushNormal.X + movement.Z * pushNormal.Z;
        if (movementAlongPushNormal >= 0.0 && penetration <= 0.01) {
            continue;
        }

        const double score = (penetration * 1000.0) + std::max(0.0, -movementAlongPushNormal) +
                             (endpointSweep ? 10.0 : 0.0);
        if (score <= bestScore) {
            continue;
        }

        bestScore = score;
        bestHit.Hit = true;
        bestHit.PolygonIndex = static_cast<int>(polyIndex);
        bestHit.Segment = segment;
        bestHit.ClosestPoint = currentClosest;
        bestHit.PushNormal = pushNormal;
        bestHit.Penetration = penetration;
        bestHit.MovementAlongPushNormal = movementAlongPushNormal;
    }

    return bestHit.Hit;
}

void CorrectLinkAgainstWallHit(LinkInstance& link, const LinkWallCollisionHit& hit) {
    if (!hit.Hit) {
        return;
    }

    const auto actor = LinkActorPosition(link);
    const auto closest = ClosestPointOnSegmentXZ(actor, hit.Segment);
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 delta = {
        actor.X - closest.X,
        0.0,
        actor.Z - closest.Z,
    };
    const double signedDistance = delta.X * hit.PushNormal.X + delta.Z * hit.PushNormal.Z;
    if (signedDistance >= link.ColliderRadius) {
        return;
    }

    const double pushDistance = link.ColliderRadius - signedDistance + 0.001;
    link.Position.X += hit.PushNormal.X * pushDistance;
    link.Position.Z += hit.PushNormal.Z * pushDistance;
}

void ApplyNativeWallSpeedLimit(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                               const LinkWallCollisionHit& hit) {
    if (!hit.Hit) {
        link.NativeWallSpeedLimitUnitsPerTick = config.RunSpeedUnitsPerTick;
        link.NativeWallSpeedScale = 1.0;
        link.NativeWorldYawToWallS16 = 0.0;
        link.NativeWallSpeedLimitActive = false;
        return;
    }

    const Vec3 movementDirection = DirectionFromYaw(link.MovementYaw);
    const Vec3 intoWall = Normalize({ -hit.PushNormal.X, 0.0, -hit.PushNormal.Z });
    const double cosAngle = std::clamp(Dot(movementDirection, intoWall), -1.0, 1.0);
    const double angle = std::acos(cosAngle);
    const double yawToWallS16 = angle / kOot3dS16AngleToRadians;
    const double speedScale = yawToWallS16 * link.NativeWallSpeedScalePerS16;

    link.NativeWorldYawToWallS16 = yawToWallS16;
    link.NativeWallSpeedScale = speedScale;
    if (!link.Grounded || speedScale >= 1.0) {
        link.NativeWallSpeedLimitUnitsPerTick = config.RunSpeedUnitsPerTick;
    } else {
        link.NativeWallSpeedLimitUnitsPerTick =
            std::max(config.RunSpeedUnitsPerTick * speedScale,
                     link.NativeMinimumWallSpeedLimitUnitsPerTick);
    }
    link.NativeWallSpeedLimitActive = link.NativeWallSpeedLimitUnitsPerTick < config.RunSpeedUnitsPerTick;
}

void ApplyLinkFloorGrounding(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, LinkInstance& link,
                             const std::optional<ThreeDsRecomp::Oot3d::Oot3dDemoVec3>& previousActor) {
    link.NativeCollisionGrounding = scene.Collision.Valid;
    link.Grounded = false;
    link.FloorPolygonIndex = -1;
    link.FloorSurfaceType = -1;
    link.NativeFloorHeightDiff = 0.0;
    if (!scene.Collision.Valid) {
        return;
    }

    const auto actor = LinkActorPosition(link);
    const double rayStartY =
        (previousActor.has_value() ? previousActor->Y : actor.Y) + kNativeFloorRaycastHeightAbovePreviousY;
    const auto hit = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(scene.Collision, actor.X, actor.Z,
                                                           rayStartY);
    if (!hit.has_value()) {
        return;
    }

    const double floorHeightDiff = hit->Y - actor.Y;
    link.NativeFloorHeightDiff = floorHeightDiff;
    if (floorHeightDiff < -kNativeGroundSnapDropLimit) {
        return;
    }

    link.Position.Y = hit->Y - link.PositionToActorOffset.Y;
    link.Grounded = true;
    link.GroundY = hit->Y;
    link.FloorPolygonIndex = hit->PolygonIndex;
    link.FloorSurfaceType = hit->SurfaceType;
}

void ApplyLinkCollisionConstrainedTranslation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                              const LinkNativeLocomotionConfig& config, LinkInstance& link,
                                              const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& delta) {
    ResetLinkHorizontalCollisionState(scene, config, link);

    const double horizontalDistance = std::sqrt(delta.X * delta.X + delta.Z * delta.Z);
    const double maxStep = std::max(1.0, link.ColliderRadius * 0.5);
    const int steps = std::clamp(static_cast<int>(std::ceil(horizontalDistance / maxStep)), 1, 64);

    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 step = {
        delta.X / static_cast<double>(steps),
        delta.Y / static_cast<double>(steps),
        delta.Z / static_cast<double>(steps),
    };
    for (int i = 0; i < steps; ++i) {
        const auto previousActor = LinkActorPosition(link);
        auto adjustedStep = step;
        LinkWallCollisionHit activeHits[kNativeWallCollisionPasses];
        int activeHitCount = 0;

        for (int pass = 0; pass < kNativeWallCollisionPasses; ++pass) {
            LinkWallCollisionHit hit;
            const auto actorCandidate = ActorPlusDelta(previousActor, adjustedStep);
            if (!FindLinkHorizontalCollision(scene, link, previousActor, actorCandidate, hit)) {
                break;
            }

            bool duplicate = false;
            for (int hitIndex = 0; hitIndex < activeHitCount; ++hitIndex) {
                duplicate = duplicate || activeHits[hitIndex].PolygonIndex == hit.PolygonIndex;
            }
            if (duplicate) {
                break;
            }

            activeHits[activeHitCount++] = hit;
            link.HorizontalCollision = true;
            link.HorizontalCollisionIterationCount++;
            if (link.WallPolygonIndex < 0) {
                link.WallPolygonIndex = hit.PolygonIndex;
                link.WallPushNormalX = hit.PushNormal.X;
                link.WallPushNormalZ = hit.PushNormal.Z;
            }

            const double intoWall =
                adjustedStep.X * hit.PushNormal.X + adjustedStep.Z * hit.PushNormal.Z;
            if (intoWall >= 0.0) {
                break;
            }
            adjustedStep.X -= hit.PushNormal.X * intoWall;
            adjustedStep.Z -= hit.PushNormal.Z * intoWall;
        }

        link.Position.X += adjustedStep.X;
        link.Position.Y += adjustedStep.Y;
        link.Position.Z += adjustedStep.Z;

        for (int hitIndex = 0; hitIndex < activeHitCount; ++hitIndex) {
            CorrectLinkAgainstWallHit(link, activeHits[hitIndex]);
        }
        ApplyLinkFloorGrounding(scene, link, previousActor);
        if (activeHitCount > 0) {
            ApplyNativeWallSpeedLimit(link, config, activeHits[0]);
        }
    }
}

LinkNativeLedgeQueryResult FindLinkNativeLedge(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config) {
    LinkNativeLedgeQueryResult result;
    if (!scene.Collision.Valid) {
        result.Status = LinkNativeLedgeQueryStatus::NoNativeCollision;
        return result;
    }

    const auto actor = LinkActorPosition(link);
    const Vec3 facing = DirectionFromYaw(link.Yaw);
    const double wallProbeDistance = config.WallCheckRadius + config.WallProbeForwardAddend;
    const Vec3 wallProbeStart = { actor.X, actor.Y + config.LedgeWallProbeHeight, actor.Z };
    const Vec3 wallProbeEnd = Add(wallProbeStart, Scale(facing, wallProbeDistance));
    NativeCollisionSegmentHit wallHit;
    if (!FindNearestEntityCollisionSegmentHit(scene.Collision, wallProbeStart, wallProbeEnd,
                                              true, wallHit)) {
        result.Status = LinkNativeLedgeQueryStatus::NoWall;
        return result;
    }

    result.WallPolygonIndex = wallHit.PolygonIndex;
    result.WallHitPosition = { wallHit.Position.X, wallHit.Position.Y, wallHit.Position.Z };
    const auto& wallPoly = scene.Collision.Polygons[static_cast<size_t>(wallHit.PolygonIndex)];
    if (std::abs(wallPoly.NormalY) >= config.WallNormalYAbsMaximumExclusive) {
        result.Status = LinkNativeLedgeQueryStatus::WallNormalRejected;
        return result;
    }

    Vec3 pushNormal = wallHit.Normal;
    pushNormal.Y = 0.0;
    pushNormal = Normalize(pushNormal);
    const Vec3 intoWall = Scale(pushNormal, -1.0);
    if (AngleDifferenceS16(facing, intoWall) >= config.ShapeYawToWallMaximumS16) {
        result.Status = LinkNativeLedgeQueryStatus::WallFacingRejected;
        return result;
    }
    result.WallPushNormal = { pushNormal.X, 0.0, pushNormal.Z };
    const Vec3 wallDelta = Subtract(wallHit.Position, wallProbeStart);
    result.DistanceToWall = std::sqrt(wallDelta.X * wallDelta.X + wallDelta.Z * wallDelta.Z);

    const double ledgeOffset = result.DistanceToWall + config.WallProbeForwardAddend;
    result.LedgeProbePosition = {
        actor.X - ledgeOffset * pushNormal.X,
        actor.Y + config.LedgeFloorProbeHeight,
        actor.Z - ledgeOffset * pushNormal.Z,
    };
    const auto floorHit = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(
        scene.Collision, result.LedgeProbePosition.X, result.LedgeProbePosition.Z,
        result.LedgeProbePosition.Y);
    if (!floorHit.has_value()) {
        result.Status = LinkNativeLedgeQueryStatus::NoLedgeFloor;
        return result;
    }
    result.LedgeFloorPolygonIndex = floorHit->PolygonIndex;
    result.YDistance = floorHit->Y - actor.Y;
    if (result.YDistance < config.MinimumLedgeY) {
        result.Status = LinkNativeLedgeQueryStatus::LedgeTooLow;
        return result;
    }

    NativeCollisionSegmentHit ceilingHit;
    const Vec3 clearanceStart = { actor.X, actor.Y + 0.001, actor.Z };
    const Vec3 clearanceEnd = {
        actor.X, floorHit->Y + config.CeilingClearanceAboveLedge, actor.Z
    };
    if (FindNearestEntityCollisionSegmentHit(scene.Collision, clearanceStart, clearanceEnd,
                                             false, ceilingHit, wallHit.PolygonIndex)) {
        result.Status = LinkNativeLedgeQueryStatus::CeilingBlocked;
        return result;
    }

    const Vec3 upperProbeStart = {
        actor.X, floorHit->Y + config.UpperWallProbeAboveLedge, actor.Z
    };
    const Vec3 upperProbeEnd = Add(upperProbeStart, Scale(facing, wallProbeDistance));
    NativeCollisionSegmentHit upperWallHit;
    if (FindNearestEntityCollisionSegmentHit(scene.Collision, upperProbeStart, upperProbeEnd,
                                             true, upperWallHit)) {
        result.UpperWallPolygonIndex = upperWallHit.PolygonIndex;
        result.UpperWallFlags = NativeSurfaceWallFlags(
            scene.Collision, upperWallHit.PolygonIndex, config);
        if (AngleDifferenceS16(wallHit.Normal, upperWallHit.Normal) <
                config.UpperWallYawDifferenceMaximumS16 &&
            (result.UpperWallFlags & config.UpperWallClearanceFlagMask) == 0) {
            result.Status = LinkNativeLedgeQueryStatus::UpperWallBlocked;
            return result;
        }
    }

    result.WallFlags = NativeSurfaceWallFlags(scene.Collision, wallHit.PolygonIndex, config);
    if ((result.WallFlags & config.CurrentWallRejectFlagMask) != 0) {
        result.Status = LinkNativeLedgeQueryStatus::WallSurfaceRejected;
        return result;
    }

    if (result.YDistance < config.LedgeType2MinimumY) {
        result.ClimbType = 1;
    } else {
        const auto& floorPoly = scene.Collision.Polygons[static_cast<size_t>(floorHit->PolygonIndex)];
        if (std::abs(floorPoly.NormalY) <= config.FloorNormalYAbsMinimumExclusive) {
            result.Status = LinkNativeLedgeQueryStatus::LedgeFloorNormalRejected;
            return result;
        }
        if (result.YDistance >= config.LedgeType4MinimumY) {
            result.ClimbType = 4;
        } else if (result.YDistance >= config.LedgeType3MinimumY) {
            result.ClimbType = 3;
        } else {
            result.ClimbType = 2;
        }
    }

    result.HangActorPosition = {
        actor.X - (result.DistanceToWall + config.HangWallPlaneOffset) * pushNormal.X,
        actor.Y,
        actor.Z - (result.DistanceToWall + config.HangWallPlaneOffset) * pushNormal.Z,
    };
    result.HangActorPosition.Y = floorHit->Y;
    result.TopActorPosition = {
        result.LedgeProbePosition.X,
        floorHit->Y,
        result.LedgeProbePosition.Z,
    };
    result.Available = true;
    result.Status = LinkNativeLedgeQueryStatus::Ready;
    return result;
}

const char* LinkNativeLedgeQueryStatusName(LinkNativeLedgeQueryStatus status) {
    switch (status) {
        case LinkNativeLedgeQueryStatus::Unavailable: return "unavailable";
        case LinkNativeLedgeQueryStatus::Ready: return "ready";
        case LinkNativeLedgeQueryStatus::NoNativeCollision: return "no_native_collision";
        case LinkNativeLedgeQueryStatus::NoWall: return "no_wall";
        case LinkNativeLedgeQueryStatus::WallFacingRejected: return "wall_facing_rejected";
        case LinkNativeLedgeQueryStatus::WallNormalRejected: return "wall_normal_rejected";
        case LinkNativeLedgeQueryStatus::NoLedgeFloor: return "no_ledge_floor";
        case LinkNativeLedgeQueryStatus::LedgeTooLow: return "ledge_too_low";
        case LinkNativeLedgeQueryStatus::CeilingBlocked: return "ceiling_blocked";
        case LinkNativeLedgeQueryStatus::UpperWallBlocked: return "upper_wall_blocked";
        case LinkNativeLedgeQueryStatus::WallSurfaceRejected: return "wall_surface_rejected";
        case LinkNativeLedgeQueryStatus::LedgeFloorNormalRejected: return "ledge_floor_normal_rejected";
    }
    return "unknown";
}

namespace {

LinkNativeSurfaceClimbQueryResult FindLinkNativeClimbSurface(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config, LinkNativeClimbMode mode,
    uint32_t requiredWallFlagMask) {
    LinkNativeSurfaceClimbQueryResult result;
    if (!scene.Collision.Valid) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::NoNativeCollision;
        return result;
    }
    if (!link.HorizontalCollision || link.WallPolygonIndex < 0) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::NoWall;
        return result;
    }
    if (static_cast<size_t>(link.WallPolygonIndex) >= scene.Collision.Polygons.size()) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::InvalidWallPolygon;
        return result;
    }

    const auto& wall = scene.Collision.Polygons[static_cast<size_t>(link.WallPolygonIndex)];
    if (!CollisionPolygonIndicesValid(scene.Collision, wall)) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::InvalidWallPolygon;
        return result;
    }
    if (std::abs(wall.NormalY) >= config.WallNormalYAbsMaximumExclusive) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::WallNormalRejected;
        return result;
    }
    result.WallFlags = NativeSurfaceWallFlags(
        scene.Collision, link.WallPolygonIndex, config);
    if ((result.WallFlags & requiredWallFlagMask) == 0) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::WallSurfaceRejected;
        return result;
    }

    Vec3 pushNormal = { link.WallPushNormalX, 0.0, link.WallPushNormalZ };
    if (Dot(pushNormal, pushNormal) <= 0.000001) {
        pushNormal = {
            static_cast<double>(wall.NormalX), 0.0,
            static_cast<double>(wall.NormalZ)
        };
    }
    pushNormal = Normalize(pushNormal);
    const auto actor = LinkActorPosition(link);
    const auto& planePoint = scene.Collision.Vertices[static_cast<size_t>(wall.VertexA)];
    double signedDistance =
        (actor.X - planePoint.X) * pushNormal.X +
        (actor.Z - planePoint.Z) * pushNormal.Z;
    if (signedDistance < 0.0) {
        pushNormal = Scale(pushNormal, -1.0);
        signedDistance = -signedDistance;
    }
    result.WallPushNormal = { pushNormal.X, 0.0, pushNormal.Z };
    result.Mode = mode;

    double polygonMinimumX = std::numeric_limits<double>::infinity();
    double polygonMaximumX = -std::numeric_limits<double>::infinity();
    double polygonMinimumY = std::numeric_limits<double>::infinity();
    double polygonMinimumZ = std::numeric_limits<double>::infinity();
    double polygonMaximumZ = -std::numeric_limits<double>::infinity();
    for (const int vertexIndex : { wall.VertexA, wall.VertexB, wall.VertexC }) {
        const auto& vertex = scene.Collision.Vertices[static_cast<size_t>(vertexIndex)];
        polygonMinimumX = std::min(polygonMinimumX, vertex.X);
        polygonMaximumX = std::max(polygonMaximumX, vertex.X);
        polygonMinimumY = std::min(polygonMinimumY, vertex.Y);
        polygonMinimumZ = std::min(polygonMinimumZ, vertex.Z);
        polygonMaximumZ = std::max(polygonMaximumZ, vertex.Z);
    }

    if (mode == LinkNativeClimbMode::RegularLadder) {
        const double centerX = (polygonMinimumX + polygonMaximumX) * 0.5;
        const double centerZ = (polygonMinimumZ + polygonMaximumZ) * 0.5;
        result.LateralAlignmentDistance = std::abs(
            (actor.X - centerX) * pushNormal.Z -
            (actor.Z - centerZ) * pushNormal.X);
        if (result.LateralAlignmentDistance >=
            config.GroundClimbLateralAlignmentMaximumExclusive) {
            result.Status = LinkNativeSurfaceClimbQueryStatus::LateralAlignmentRejected;
            return result;
        }
        const double relativeY = actor.Y - polygonMinimumY;
        result.RungSnapDeltaY =
            std::floor(relativeY / config.GroundClimbRungInterval + 0.5) *
                config.GroundClimbRungInterval -
            relativeY;
        const double planeDistance = std::max(
            0.0, signedDistance - config.GroundClimbWallPlaneInset);
        result.AlignedActorPosition = {
            centerX + pushNormal.X * planeDistance,
            actor.Y + result.RungSnapDeltaY,
            centerZ + pushNormal.Z * planeDistance,
        };
    } else {
        result.AlignedActorPosition = {
            actor.X + pushNormal.X * (config.WallCheckRadius - signedDistance),
            actor.Y,
            actor.Z + pushNormal.Z * (config.WallCheckRadius - signedDistance),
        };
    }

    std::vector<bool> included(scene.Collision.Polygons.size(), false);
    std::vector<size_t> pending = { static_cast<size_t>(link.WallPolygonIndex) };
    included[static_cast<size_t>(link.WallPolygonIndex)] = true;
    result.SurfaceMinimumY = std::numeric_limits<double>::infinity();
    result.SurfaceMaximumY = -std::numeric_limits<double>::infinity();
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const auto currentIndex = pending[cursor];
        const auto& current = scene.Collision.Polygons[currentIndex];
        for (const int vertexIndex : { current.VertexA, current.VertexB, current.VertexC }) {
            const double y = scene.Collision.Vertices[static_cast<size_t>(vertexIndex)].Y;
            result.SurfaceMinimumY = std::min(result.SurfaceMinimumY, y);
            result.SurfaceMaximumY = std::max(result.SurfaceMaximumY, y);
        }
        for (size_t candidateIndex = 0;
             candidateIndex < scene.Collision.Polygons.size(); ++candidateIndex) {
            if (included[candidateIndex]) {
                continue;
            }
            const auto& candidate = scene.Collision.Polygons[candidateIndex];
            if (!CollisionPolygonIndicesValid(scene.Collision, candidate) ||
                !CollisionPolygonsShareClimbPlane(wall, candidate) ||
                !CollisionPolygonsShareEdge(current, candidate) ||
                (NativeSurfaceWallFlags(scene.Collision, static_cast<int>(candidateIndex), config) &
                 requiredWallFlagMask) == 0) {
                continue;
            }
            included[candidateIndex] = true;
            pending.push_back(candidateIndex);
        }
    }

    if (mode == LinkNativeClimbMode::RegularLadder &&
        result.SurfaceMaximumY - result.SurfaceMinimumY <
            config.GroundClimbMinimumYDistanceToLedge) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::SurfaceHeightRejected;
        return result;
    }

    if (result.AlignedActorPosition.Y <
            result.SurfaceMinimumY - config.SurfaceClimbBottomDismountFloorDelta ||
        result.AlignedActorPosition.Y >
            result.SurfaceMaximumY + config.SurfaceClimbTopReachHeight) {
        result.Status = LinkNativeSurfaceClimbQueryStatus::OutsideSurfaceHeight;
        return result;
    }
    result.WallPolygonIndex = link.WallPolygonIndex;
    result.Available = true;
    result.Status = LinkNativeSurfaceClimbQueryStatus::Ready;
    return result;
}

} // namespace

LinkNativeSurfaceClimbQueryResult FindLinkNativeSurfaceClimb(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config) {
    return FindLinkNativeClimbSurface(
        scene, link, config, LinkNativeClimbMode::FreeSurface,
        config.FreeClimbWallFlagMask);
}

LinkNativeSurfaceClimbQueryResult FindLinkNativeRegularLadder(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config) {
    return FindLinkNativeClimbSurface(
        scene, link, config, LinkNativeClimbMode::RegularLadder,
        config.RegularLadderWallFlagMask);
}

LinkNativeClimbTopFloorQueryResult FindLinkNativeClimbTopFloor(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config) {
    LinkNativeClimbTopFloorQueryResult result;
    if (!scene.Collision.Valid || link.NativeClimbMode == LinkNativeClimbMode::None) {
        return result;
    }
    const auto actor = LinkActorPosition(link);
    const Vec3 facing = DirectionFromYaw(link.Yaw);
    result.ProbePosition = {
        actor.X + facing.X * config.SurfaceClimbTopFloorProbeForwardDistance,
        actor.Y + config.SurfaceClimbTopReachHeight,
        actor.Z + facing.Z * config.SurfaceClimbTopFloorProbeForwardDistance,
    };
    const auto floor = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(
        scene.Collision, result.ProbePosition.X, result.ProbePosition.Z,
        result.ProbePosition.Y);
    if (!floor.has_value()) {
        return result;
    }
    result.Available = true;
    result.FloorPolygonIndex = floor->PolygonIndex;
    result.FloorSurfaceType = floor->SurfaceType;
    result.FloorY = floor->Y;
    return result;
}

const char* LinkNativeSurfaceClimbQueryStatusName(
    LinkNativeSurfaceClimbQueryStatus status) {
    switch (status) {
        case LinkNativeSurfaceClimbQueryStatus::Unavailable: return "unavailable";
        case LinkNativeSurfaceClimbQueryStatus::Ready: return "ready";
        case LinkNativeSurfaceClimbQueryStatus::NoNativeCollision: return "no_native_collision";
        case LinkNativeSurfaceClimbQueryStatus::NoWall: return "no_wall";
        case LinkNativeSurfaceClimbQueryStatus::InvalidWallPolygon: return "invalid_wall_polygon";
        case LinkNativeSurfaceClimbQueryStatus::WallNormalRejected: return "wall_normal_rejected";
        case LinkNativeSurfaceClimbQueryStatus::WallSurfaceRejected: return "wall_surface_rejected";
        case LinkNativeSurfaceClimbQueryStatus::SurfaceHeightRejected: return "surface_height_rejected";
        case LinkNativeSurfaceClimbQueryStatus::LateralAlignmentRejected: return "lateral_alignment_rejected";
        case LinkNativeSurfaceClimbQueryStatus::OutsideSurfaceHeight: return "outside_surface_height";
    }
    return "unknown";
}
