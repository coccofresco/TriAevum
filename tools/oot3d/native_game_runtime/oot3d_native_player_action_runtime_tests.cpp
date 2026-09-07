#include "oot3d_native_player_action_runtime.h"

#include <cmath>
#include <stdexcept>

namespace {

void ExpectNear(double actual, double expected, const char* label) {
    if (std::abs(actual - expected) > 0.000001) {
        throw std::runtime_error(std::string(label) + " expected " +
                                 std::to_string(expected) + ", got " +
                                 std::to_string(actual));
    }
}

} // namespace

void RunNativePlayerActionRuntimeTests() {
    LinkNativeLocomotionConfig config;
    LinkNativePlayerActionConfig playerActionConfig;
    playerActionConfig.StartModeMask = 0x0F00;
    playerActionConfig.StartModeShift = 8;
    playerActionConfig.SceneEntranceIdleStartMode = 13;
    playerActionConfig.SceneEntranceSlowStartMode = 14;
    playerActionConfig.SceneEntranceForwardStartMode = 15;
    playerActionConfig.SceneEntranceIdleTargetDistance = 180.0;
    playerActionConfig.SceneEntranceIdleTimer = -20;
    playerActionConfig.SceneEntranceSlowSpeedUnitsPerTick = 2.0;
    playerActionConfig.SceneEntranceSlowTargetDistance = 120.0;
    playerActionConfig.SceneEntranceSlowTimer = -15;
    playerActionConfig.SceneEntranceForwardMinimumSpeedUnitsPerTick = 0.1;
    playerActionConfig.SceneEntranceForwardTargetDistance = 800.0;
    playerActionConfig.SceneEntranceForwardTimerNumerator = -80.0;
    playerActionConfig.SceneEntranceForwardMinimumTimer = -20;
    playerActionConfig.SceneEntranceInitialLinearSpeedUnitsPerTick = 0.1;
    playerActionConfig.SceneEntranceDefaultTargetSpeedUnitsPerTick = 5.0;
    playerActionConfig.SceneEntranceFloorProbeYOffset = 50.0;
    playerActionConfig.SceneEntranceTargetCaptureDistance = 30;
    playerActionConfig.SceneEntranceCompletionDistance = 20;

    LinkInstance entrance;
    entrance.Yaw = kOot3dDemoPi * 0.5;
    if (!Oot3dNativeGame::BeginNativePlayerSceneEntrance(
            entrance, 0x0DFF, playerActionConfig)) {
        throw std::runtime_error("native idle scene entrance was not selected");
    }
    if (entrance.NativePlayerAction != LinkNativePlayerAction::SceneEntrance ||
        entrance.NativeSceneEntranceStartMode != 13 ||
        entrance.NativeSceneEntranceTimer != -20 ||
        !entrance.NativeSceneEntranceTargetCaptured) {
        throw std::runtime_error("native idle scene entrance state is incomplete");
    }
    ExpectNear(entrance.NativeSceneEntranceTargetActorPosition.X, 180.0,
               "native idle scene entrance target x");
    ExpectNear(entrance.NativeSceneEntranceTargetActorPosition.Z, 0.0,
               "native idle scene entrance target z");
    Oot3dNativeGame::AdvanceNativePlayerSceneEntrance(
        entrance, playerActionConfig, 30.0, 19.0 / 30.0);
    if (entrance.NativeSceneEntranceTimer != -1 ||
        Oot3dNativeGame::NativePlayerSceneEntranceShouldComplete(entrance)) {
        throw std::runtime_error("native scene entrance timer completed too early");
    }
    Oot3dNativeGame::AdvanceNativePlayerSceneEntrance(
        entrance, playerActionConfig, 30.0, 1.0 / 30.0);
    if (!Oot3dNativeGame::NativePlayerSceneEntranceShouldComplete(entrance)) {
        throw std::runtime_error("native scene entrance timer did not complete");
    }
    Oot3dNativeGame::CompleteNativePlayerSceneEntrance(entrance, config);
    if (entrance.NativePlayerAction != LinkNativePlayerAction::Locomotion ||
        entrance.NativeSpeedTargetUnitsPerTick != config.RunSpeedUnitsPerTick) {
        throw std::runtime_error("native scene entrance did not return to locomotion");
    }

    LinkInstance slowEntrance;
    if (!Oot3dNativeGame::BeginNativePlayerSceneEntrance(
            slowEntrance, 0x0EFF, playerActionConfig)) {
        throw std::runtime_error("native slow scene entrance was not selected");
    }
    ExpectNear(slowEntrance.NativeLinearVelocityUnitsPerTick, 2.0,
               "native slow scene entrance speed");
    if (slowEntrance.NativeSceneEntranceTimer != -15) {
        throw std::runtime_error("native slow scene entrance timer is wrong");
    }

    LinkInstance forwardEntrance;
    if (!Oot3dNativeGame::BeginNativePlayerSceneEntrance(
            forwardEntrance, 0x0FFF, playerActionConfig)) {
        throw std::runtime_error("native forward scene entrance was not selected");
    }
    ExpectNear(forwardEntrance.NativeLinearVelocityUnitsPerTick, 0.1,
               "native forward minimum entrance speed");
    if (forwardEntrance.NativeSceneEntranceTimer != -20) {
        throw std::runtime_error("native forward entrance timer clamp is wrong");
    }
    LinkInstance fastForwardEntrance;
    fastForwardEntrance.NativeLinearVelocityUnitsPerTick = 8.0;
    Oot3dNativeGame::BeginNativePlayerSceneEntrance(
        fastForwardEntrance, 0x0FFF, playerActionConfig);
    if (fastForwardEntrance.NativeSceneEntranceTimer != -10) {
        throw std::runtime_error("native forward entrance timer division is wrong");
    }
    LinkInstance unsupportedEntrance;
    if (Oot3dNativeGame::BeginNativePlayerSceneEntrance(
            unsupportedEntrance, 0x0CFF, playerActionConfig)) {
        throw std::runtime_error("unsupported native scene entrance was accepted");
    }

    LinkInstance runAutoJump;
    runAutoJump.Grounded = false;
    runAutoJump.NativeFloorHeightDiff = -30.0;
    runAutoJump.NativeLinearVelocityUnitsPerTick = 5.5;
    runAutoJump.Yaw = 0.0;
    runAutoJump.MovementYaw = 0.0;
    if (!Oot3dNativeGame::NativePlayerCanAutoJump(runAutoJump, config, true)) {
        throw std::runtime_error("valid native run auto-jump was rejected");
    }
    Oot3dNativeGame::BeginNativePlayerAutoJump(runAutoJump, config, 100.0);
    if (runAutoJump.NativePlayerAction != LinkNativePlayerAction::AutoJump ||
        runAutoJump.NativeAutoJumpKind != LinkNativeAutoJumpKind::Run ||
        !Oot3dNativeGame::NativePlayerIsAirborne(runAutoJump)) {
        throw std::runtime_error("native run auto-jump state is incomplete");
    }
    ExpectNear(runAutoJump.NativeVerticalVelocityUnitsPerTick, 7.5,
               "native high-speed auto-jump velocity");
    if (Oot3dNativeGame::NativePlayerShouldUseLandingAnticipation(runAutoJump)) {
        throw std::runtime_error("rising native auto-jump selected landing anticipation");
    }
    runAutoJump.NativeVerticalVelocityUnitsPerTick = -1.0;
    runAutoJump.NativeFallDistance = 1.0;
    if (!Oot3dNativeGame::NativePlayerShouldUseLandingAnticipation(runAutoJump)) {
        throw std::runtime_error("descending native auto-jump missed landing anticipation");
    }
    Oot3dNativeGame::BeginNativePlayerLanding(runAutoJump, config, 100.0);
    if (runAutoJump.NativeLandingKind != LinkNativeLandingKind::RunAutoJump) {
        throw std::runtime_error("native run auto-jump landing clip was not selected");
    }

    LinkInstance normalAutoJump;
    normalAutoJump.Grounded = false;
    normalAutoJump.NativeFloorHeightDiff = -30.0;
    normalAutoJump.NativeLinearVelocityUnitsPerTick = 4.0;
    Oot3dNativeGame::BeginNativePlayerAutoJump(normalAutoJump, config, 100.0);
    if (normalAutoJump.NativeAutoJumpKind != LinkNativeAutoJumpKind::Normal) {
        throw std::runtime_error("native normal auto-jump was misclassified");
    }
    ExpectNear(normalAutoJump.NativeVerticalVelocityUnitsPerTick, 2.85,
               "native speed-scaled auto-jump velocity");
    normalAutoJump = {};
    normalAutoJump.Grounded = false;
    normalAutoJump.NativeFloorHeightDiff = -15.0;
    normalAutoJump.NativeLinearVelocityUnitsPerTick = 5.5;
    if (Oot3dNativeGame::NativePlayerCanAutoJump(normalAutoJump, config, true)) {
        throw std::runtime_error("small native floor drop incorrectly triggered auto-jump");
    }

    LinkInstance link;
    Oot3dNativeGame::BeginNativePlayerAirborne(link, 100.0);

    auto step = Oot3dNativeGame::AdvanceNativePlayerAirborne(link, config, 1.0 / 60.0);
    if (step.TickCount != 0) {
        throw std::runtime_error("native player physics advanced before one 30 Hz tick");
    }
    step = Oot3dNativeGame::AdvanceNativePlayerAirborne(link, config, 1.0 / 60.0);
    if (step.TickCount != 1) {
        throw std::runtime_error("native player physics did not advance at 30 Hz");
    }
    ExpectNear(step.DeltaY, -1.0, "first native fall delta");
    ExpectNear(step.VelocityYUnitsPerTick, -1.0, "first native fall velocity");

    step = Oot3dNativeGame::AdvanceNativePlayerAirborne(link, config, 19.0 / 30.0);
    ExpectNear(step.VelocityYUnitsPerTick, -20.0, "native terminal fall velocity");
    step = Oot3dNativeGame::AdvanceNativePlayerAirborne(link, config, 3.0 / 30.0);
    ExpectNear(step.DeltaY, -60.0, "terminal velocity integration");

    Oot3dNativeGame::BeginNativePlayerAirborne(link, 100.0);
    Oot3dNativeGame::BeginNativePlayerLanding(link, config, 30.0);
    if (link.NativePlayerAction != LinkNativePlayerAction::Landing ||
        link.NativeLandingKind != LinkNativeLandingKind::Short) {
        throw std::runtime_error("short native landing was not selected");
    }

    Oot3dNativeGame::BeginNativePlayerAirborne(link, 100.0);
    Oot3dNativeGame::BeginNativePlayerLanding(link, config, 10.0);
    if (link.NativeLandingKind != LinkNativeLandingKind::Normal) {
        throw std::runtime_error("normal native landing was not selected");
    }
    link.GroundY = 10.0;
    Oot3dNativeGame::CompleteNativePlayerLanding(link);
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion ||
        link.NativeFallDistance != 0.0) {
        throw std::runtime_error("native landing did not return to locomotion");
    }

    LinkNativeCollisionActionConfig actionConfig;
    actionConfig.MinimumClimbTypeForFallingGrab = 2;
    actionConfig.FallingGrabRequiresDescending = true;
    actionConfig.FallingGrabRequiresForwardSpeed = true;
    actionConfig.FallingGrabMaximumFallDistance = 150.0;
    actionConfig.FallingGrabMinimumLedgeAboveFloor = 44.8;
    LinkNativeLedgeQueryResult ledge;
    ledge.Available = true;
    ledge.ClimbType = 3;
    ledge.WallPolygonIndex = 4;
    ledge.LedgeFloorPolygonIndex = 7;
    ledge.YDistance = 43.0;
    ledge.HangActorPosition = { 10.0, 43.0, 20.0 };
    ledge.TopActorPosition = { 10.0, 43.0, 0.0 };
    ledge.WallPushNormal = { 0.0, 0.0, 1.0 };
    link.NativePlayerAction = LinkNativePlayerAction::Airborne;
    link.NativeVerticalVelocityUnitsPerTick = -4.0;
    link.NativeLinearVelocityUnitsPerTick = 2.0;
    link.NativeFallDistance = 20.0;
    link.Position.Y = 20.0;
    link.GroundY = 0.0;
    if (!Oot3dNativeGame::NativePlayerCanGrabLedge(link, actionConfig, ledge)) {
        throw std::runtime_error("valid native falling ledge grab was rejected");
    }
    Oot3dNativeGame::BeginNativePlayerLedgeHold(link, ledge);
    if (link.NativePlayerAction != LinkNativePlayerAction::LedgeHold ||
        link.NativeLedgeClimbType != 3 || link.Position.Y != 43.0) {
        throw std::runtime_error("native ledge hold did not capture detector state");
    }
    Oot3dNativeGame::BeginNativePlayerLedgeClimb(link);
    Oot3dNativeGame::CompleteNativePlayerLedgeClimb(link);
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion ||
        !link.Grounded || link.Position.Z != 0.0) {
        throw std::runtime_error("native ledge climb did not finish on detector top position");
    }

    LinkNativeSurfaceClimbQueryResult climbSurface;
    climbSurface.Available = true;
    climbSurface.Status = LinkNativeSurfaceClimbQueryStatus::Ready;
    climbSurface.WallPolygonIndex = 12;
    climbSurface.Mode = LinkNativeClimbMode::FreeSurface;
    climbSurface.WallFlags = 8;
    climbSurface.SurfaceMinimumY = 10.0;
    climbSurface.SurfaceMaximumY = 80.0;
    climbSurface.AlignedActorPosition = { 5.0, 20.0, 14.0 };
    climbSurface.WallPushNormal = { 0.0, 0.0, 1.0 };
    Oot3dNativeGame::BeginNativePlayerSurfaceClimb(link, climbSurface);
    if (link.NativePlayerAction != LinkNativePlayerAction::SurfaceClimb ||
        link.NativeClimbMode != LinkNativeClimbMode::FreeSurface ||
        link.NativeSurfaceClimbWallFlags != 8 || link.Grounded ||
        link.Position.X != 5.0 || link.Position.Z != 14.0) {
        throw std::runtime_error("native surface climb did not capture decoded wall state");
    }
    Oot3dNativeGame::CompleteNativePlayerSurfaceClimb(link, true, 10.0);
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion ||
        !link.Grounded || link.Position.Y != 10.0 ||
        link.NativeSurfaceClimbWallPolygonIndex != -1 ||
        link.NativeClimbMode != LinkNativeClimbMode::None) {
        throw std::runtime_error("native surface climb did not return to grounded locomotion");
    }
}
