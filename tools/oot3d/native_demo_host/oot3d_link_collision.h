#pragma once

#include <optional>

#include "oot3d_link_instance.h"
#include "oot3d_link_locomotion_config.h"

struct LinkCollisionSegmentXZ {
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 A;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 B;
    bool Valid = false;
};

struct LinkWallCollisionHit {
    bool Hit = false;
    int PolygonIndex = -1;
    LinkCollisionSegmentXZ Segment;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ClosestPoint;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 PushNormal;
    double Penetration = 0.0;
    double MovementAlongPushNormal = 0.0;
};

enum class LinkNativeLedgeQueryStatus {
    Unavailable,
    Ready,
    NoNativeCollision,
    NoWall,
    WallFacingRejected,
    WallNormalRejected,
    NoLedgeFloor,
    LedgeTooLow,
    CeilingBlocked,
    UpperWallBlocked,
    WallSurfaceRejected,
    LedgeFloorNormalRejected,
};

struct LinkNativeLedgeQueryResult {
    LinkNativeLedgeQueryStatus Status = LinkNativeLedgeQueryStatus::Unavailable;
    bool Available = false;
    int WallPolygonIndex = -1;
    int LedgeFloorPolygonIndex = -1;
    int UpperWallPolygonIndex = -1;
    int ClimbType = 0;
    uint32_t WallFlags = 0;
    uint32_t UpperWallFlags = 0;
    double DistanceToWall = 0.0;
    double YDistance = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 WallHitPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 LedgeProbePosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 HangActorPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 TopActorPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 WallPushNormal;
};

enum class LinkNativeSurfaceClimbQueryStatus {
    Unavailable,
    Ready,
    NoNativeCollision,
    NoWall,
    InvalidWallPolygon,
    WallNormalRejected,
    WallSurfaceRejected,
    SurfaceHeightRejected,
    LateralAlignmentRejected,
    OutsideSurfaceHeight,
};

struct LinkNativeSurfaceClimbQueryResult {
    LinkNativeSurfaceClimbQueryStatus Status =
        LinkNativeSurfaceClimbQueryStatus::Unavailable;
    bool Available = false;
    int WallPolygonIndex = -1;
    LinkNativeClimbMode Mode = LinkNativeClimbMode::None;
    uint32_t WallFlags = 0;
    double SurfaceMinimumY = 0.0;
    double SurfaceMaximumY = 0.0;
    double LateralAlignmentDistance = 0.0;
    double RungSnapDeltaY = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 WallPushNormal;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 AlignedActorPosition;
};

struct LinkNativeClimbTopFloorQueryResult {
    bool Available = false;
    int FloorPolygonIndex = -1;
    int FloorSurfaceType = -1;
    double FloorY = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ProbePosition;
};

void ResetLinkHorizontalCollisionState(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                       const LinkNativeLocomotionConfig& config, LinkInstance& link);
bool FindLinkHorizontalCollision(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
                                 const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& previousActor,
                                 const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actor,
                                 LinkWallCollisionHit& bestHit);
void CorrectLinkAgainstWallHit(LinkInstance& link, const LinkWallCollisionHit& hit);
void ApplyNativeWallSpeedLimit(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                               const LinkWallCollisionHit& hit);
void ApplyLinkFloorGrounding(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, LinkInstance& link,
                             const std::optional<ThreeDsRecomp::Oot3d::Oot3dDemoVec3>& previousActor = std::nullopt);
void ApplyLinkCollisionConstrainedTranslation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                              const LinkNativeLocomotionConfig& config, LinkInstance& link,
                                              const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& delta);
LinkNativeLedgeQueryResult FindLinkNativeLedge(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config);
const char* LinkNativeLedgeQueryStatusName(LinkNativeLedgeQueryStatus status);
LinkNativeSurfaceClimbQueryResult FindLinkNativeSurfaceClimb(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config);
LinkNativeSurfaceClimbQueryResult FindLinkNativeRegularLadder(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config);
LinkNativeClimbTopFloorQueryResult FindLinkNativeClimbTopFloor(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
    const LinkNativeCollisionActionConfig& config);
const char* LinkNativeSurfaceClimbQueryStatusName(
    LinkNativeSurfaceClimbQueryStatus status);

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ActorPlusDelta(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actor,
                                          const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& delta);
