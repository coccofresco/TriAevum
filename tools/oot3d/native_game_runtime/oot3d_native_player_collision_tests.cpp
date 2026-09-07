#include "oot3d_link_collision.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

LinkNativeCollisionActionConfig TestConfig() {
    LinkNativeCollisionActionConfig config;
    config.WallCheckRadius = 14.0;
    config.StandingWallCheckHeight = 26.0;
    config.LedgeWallProbeHeight = 18.0;
    config.WallProbeForwardAddend = 10.0;
    config.LedgeFloorProbeHeight = 71.0;
    config.MinimumLedgeY = 18.0;
    config.InvalidLedgeY = 399.96002197265625;
    config.CeilingClearanceAboveLedge = 20.0;
    config.UpperWallProbeAboveLedge = 5.0;
    config.LedgeType2MinimumY = 27.0;
    config.LedgeType3MinimumY = 39.0;
    config.LedgeType4MinimumY = 47.0;
    config.ShapeYawToWallMaximumS16 = 0x3000;
    config.UpperWallYawDifferenceMaximumS16 = 0x4000;
    config.WallNormalYAbsMaximumExclusive = 600;
    config.FloorNormalYAbsMinimumExclusive = 28000;
    config.CurrentWallRejectFlagMask = 1;
    config.UpperWallClearanceFlagMask = 2;
    config.MinimumClimbTypeForFallingGrab = 2;
    config.FallingGrabMinimumLedgeAboveFloor = 44.8;
    config.HangWallPlaneOffset = 1.0;
    config.FreeClimbWallFlagMask = 8;
    config.RegularLadderWallFlagMask = 2;
    config.RegularLadderModeValue = 0;
    config.FreeClimbModeValue = 2;
    config.GroundClimbInitialPhase = -2;
    config.GroundClimbMinimumYDistanceToLedge = 79.0;
    config.GroundClimbLateralAlignmentMaximumExclusive = 8.0;
    config.GroundClimbRungInterval = 15.0;
    config.GroundClimbWallPlaneInset = 1.0;
    config.SurfaceClimbBottomDismountFloorDelta = 15.0;
    config.SurfaceClimbTopReachHeight = 55.0;
    config.SurfaceClimbTopFloorProbeForwardDistance = 26.0;
    config.SurfaceClimbDismountPlaySpeed = 4.0 / 3.0;
    config.SurfaceBehaviorIndexShift = 21;
    config.SurfaceBehaviorIndexMask = 0x1F;
    config.SurfaceWallFlags = { 0, 1, 3, 5, 8, 16, 32, 64 };
    return config;
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon Polygon(
    int a, int b, int c, int normalX, int normalY, int normalZ, int dist) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon poly;
    poly.Type = 0;
    poly.VertexA = a;
    poly.VertexB = b;
    poly.VertexC = c;
    poly.NormalX = normalX;
    poly.NormalY = normalY;
    poly.NormalZ = normalZ;
    poly.Dist = dist;
    return poly;
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene TestLedgeScene() {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
    scene.Collision.Valid = true;
    scene.Collision.DecodedFromNativeZsi = true;
    scene.Collision.SurfaceTypes.push_back({});
    scene.Collision.Vertices = {
        { -100.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 },
        { -100.0, 43.0, 0.0 }, { 100.0, 43.0, 0.0 },
        { -100.0, 43.0, -100.0 }, { 100.0, 43.0, -100.0 },
    };
    scene.Collision.Polygons = {
        Polygon(0, 1, 2, 0, 0, 32767, 0),
        Polygon(1, 3, 2, 0, 0, 32767, 0),
        Polygon(2, 3, 4, 0, 32767, 0, -43),
        Polygon(3, 5, 4, 0, 32767, 0, -43),
    };
    return scene;
}

void ExpectNear(double actual, double expected, const char* label) {
    if (std::abs(actual - expected) > 0.001) {
        throw std::runtime_error(std::string(label) + " expected " +
                                 std::to_string(expected) + ", got " +
                                 std::to_string(actual));
    }
}

} // namespace

void RunNativePlayerCollisionTests() {
    auto scene = TestLedgeScene();
    auto config = TestConfig();
    LinkInstance link;
    link.Position = { 0.0, 0.0, 14.0 };
    link.Yaw = kOot3dDemoPi;
    link.ColliderRadius = config.WallCheckRadius;

    const auto ledge = FindLinkNativeLedge(scene, link, config);
    if (!ledge.Available || ledge.Status != LinkNativeLedgeQueryStatus::Ready ||
        ledge.ClimbType != 3 || ledge.WallPolygonIndex < 0 ||
        ledge.LedgeFloorPolygonIndex < 0) {
        throw std::runtime_error(
            std::string("native ledge detector rejected a valid type-3 ledge: ") +
            LinkNativeLedgeQueryStatusName(ledge.Status));
    }
    ExpectNear(ledge.DistanceToWall, 14.0, "native distance to wall");
    ExpectNear(ledge.YDistance, 43.0, "native ledge Y distance");
    ExpectNear(ledge.HangActorPosition.Z, -1.0, "native ledge hang alignment Z");
    ExpectNear(ledge.TopActorPosition.Z, -10.0, "native ledge top probe Z");

    auto sentinelConfig = config;
    sentinelConfig.InvalidLedgeY = 40.0;
    const auto sentinelIndependent = FindLinkNativeLedge(scene, link, sentinelConfig);
    if (!sentinelIndependent.Available ||
        sentinelIndependent.Status != LinkNativeLedgeQueryStatus::Ready) {
        throw std::runtime_error(
            "native invalid-ledge sentinel was incorrectly treated as a maximum height");
    }

    scene.Collision.SurfaceTypes[0].Data1 = 4u << config.SurfaceBehaviorIndexShift;
    link.HorizontalCollision = true;
    link.WallPolygonIndex = 0;
    link.WallPushNormalZ = 1.0;
    const auto climbSurface = FindLinkNativeSurfaceClimb(scene, link, config);
    if (!climbSurface.Available ||
        climbSurface.Status != LinkNativeSurfaceClimbQueryStatus::Ready ||
        climbSurface.WallFlags != 8) {
        throw std::runtime_error("native free-climb wall surface was not decoded");
    }
    ExpectNear(climbSurface.SurfaceMinimumY, 0.0, "native climb surface minimum Y");
    ExpectNear(climbSurface.SurfaceMaximumY, 43.0, "native climb surface maximum Y");
    ExpectNear(climbSurface.AlignedActorPosition.Z, 14.0,
               "native climb wall alignment Z");

    for (size_t vertexIndex = 2; vertexIndex < scene.Collision.Vertices.size(); ++vertexIndex) {
        scene.Collision.Vertices[vertexIndex].Y = 100.0;
    }
    scene.Collision.Polygons[2].Dist = -100;
    scene.Collision.Polygons[3].Dist = -100;
    scene.Collision.SurfaceTypes[0].Data1 = 2u << config.SurfaceBehaviorIndexShift;
    link.Position = { 0.0, 22.0, 14.0 };
    const auto ladder = FindLinkNativeRegularLadder(scene, link, config);
    if (!ladder.Available || ladder.Mode != LinkNativeClimbMode::RegularLadder ||
        ladder.WallFlags != 3) {
        throw std::runtime_error("native regular-ladder wall surface was not decoded");
    }
    ExpectNear(ladder.AlignedActorPosition.Y, 15.0, "native ladder rung snap Y");
    ExpectNear(ladder.AlignedActorPosition.Z, 13.0, "native ladder wall-plane inset Z");

    link.Position.X = 8.0;
    const auto lateralRejected = FindLinkNativeRegularLadder(scene, link, config);
    if (lateralRejected.Available ||
        lateralRejected.Status != LinkNativeSurfaceClimbQueryStatus::LateralAlignmentRejected) {
        throw std::runtime_error("native ladder lateral tolerance was not enforced");
    }

    link.Position = { 0.0, 50.0, 13.0 };
    link.NativeClimbMode = LinkNativeClimbMode::RegularLadder;
    const auto topFloor = FindLinkNativeClimbTopFloor(scene, link, config);
    if (!topFloor.Available || topFloor.FloorPolygonIndex < 0) {
        throw std::runtime_error("native ladder top-floor probe did not find the upper floor");
    }
    ExpectNear(topFloor.FloorY, 100.0, "native ladder top-floor height");

    scene.Collision.SurfaceTypes[0].Data1 = 1u << config.SurfaceBehaviorIndexShift;
    const auto rejected = FindLinkNativeLedge(scene, link, config);
    if (rejected.Available ||
        rejected.Status != LinkNativeLedgeQueryStatus::WallSurfaceRejected) {
        throw std::runtime_error("native wall surface flag did not reject ledge grab");
    }
}
