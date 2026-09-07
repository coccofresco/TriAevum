#include "oot3d_native_player_action_runtime.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

void SetActorPosition(LinkInstance& link,
                      const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actorPosition) {
    link.Position = {
        actorPosition.X - link.PositionToActorOffset.X,
        actorPosition.Y - link.PositionToActorOffset.Y,
        actorPosition.Z - link.PositionToActorOffset.Z,
    };
}

void ClearLedgeState(LinkInstance& link) {
    link.NativeLedgeWallPolygonIndex = -1;
    link.NativeLedgeFloorPolygonIndex = -1;
    link.NativeLedgeClimbType = 0;
    link.NativeLedgeYDistance = 0.0;
    link.NativeLedgeHangActorPosition = {};
    link.NativeLedgeTopActorPosition = {};
    link.NativeLedgePushNormalX = 0.0;
    link.NativeLedgePushNormalZ = 0.0;
}

void ClearSurfaceClimbState(LinkInstance& link) {
    link.NativeSurfaceClimbWallPolygonIndex = -1;
    link.NativeClimbMode = LinkNativeClimbMode::None;
    link.NativeSurfaceClimbWallFlags = 0;
    link.NativeSurfaceClimbMinimumY = 0.0;
    link.NativeSurfaceClimbMaximumY = 0.0;
    link.NativeSurfaceClimbAnchorActorPosition = {};
    link.NativeSurfaceClimbPushNormalX = 0.0;
    link.NativeSurfaceClimbPushNormalZ = 0.0;
    link.NativeSurfaceClimbTopFloorPolygonIndex = -1;
    link.NativeSurfaceClimbTopFloorY = 0.0;
    link.NativeSurfaceClimbQueryStatus = "unavailable";
}

double SceneEntranceDistance(const LinkInstance& link) {
    const auto actor = LinkActorPosition(link);
    const double deltaX = link.NativeSceneEntranceTargetActorPosition.X - actor.X;
    const double deltaZ = link.NativeSceneEntranceTargetActorPosition.Z - actor.Z;
    return std::sqrt((deltaX * deltaX) + (deltaZ * deltaZ));
}

double PlayerMovementYawDifferenceS16(const LinkInstance& link) {
    return std::abs(NormalizeAngleRadians(link.Yaw - link.MovementYaw)) /
           kOot3dS16AngleToRadians;
}

} // namespace

bool BeginNativePlayerSceneEntrance(
    LinkInstance& link, int playerParams,
    const LinkNativePlayerActionConfig& config) {
    if (config.StartModeMask == 0 || config.StartModeShift >= 16) {
        throw std::invalid_argument("invalid OOT3D player start-mode selector");
    }

    const uint32_t params = static_cast<uint16_t>(playerParams);
    const int startMode = static_cast<int>(
        (params & config.StartModeMask) >> config.StartModeShift);
    double targetDistance = 0.0;
    double entranceSpeed = link.NativeLinearVelocityUnitsPerTick;
    int timer = 0;
    if (startMode == config.SceneEntranceIdleStartMode) {
        targetDistance = config.SceneEntranceIdleTargetDistance;
        timer = config.SceneEntranceIdleTimer;
    } else if (startMode == config.SceneEntranceSlowStartMode) {
        targetDistance = config.SceneEntranceSlowTargetDistance;
        entranceSpeed = config.SceneEntranceSlowSpeedUnitsPerTick;
        timer = config.SceneEntranceSlowTimer;
    } else if (startMode == config.SceneEntranceForwardStartMode) {
        targetDistance = config.SceneEntranceForwardTargetDistance;
        entranceSpeed = std::max(
            entranceSpeed, config.SceneEntranceForwardMinimumSpeedUnitsPerTick);
        timer = static_cast<int>(
            config.SceneEntranceForwardTimerNumerator / entranceSpeed);
        timer = std::max(timer, config.SceneEntranceForwardMinimumTimer);
    } else {
        return false;
    }

    if (!std::isfinite(targetDistance) || targetDistance <= 0.0 ||
        !std::isfinite(entranceSpeed) || entranceSpeed < 0.0 || timer >= 0) {
        throw std::invalid_argument("invalid OOT3D player scene-entrance state");
    }

    const auto actor = LinkActorPosition(link);
    const Vec3 direction = DirectionFromYaw(link.Yaw);
    link.NativePlayerAction = LinkNativePlayerAction::SceneEntrance;
    link.NativeSceneEntranceStartMode = startMode;
    link.NativeSceneEntranceTimer = timer;
    link.NativeSceneEntranceTickAccumulator = 0.0;
    link.NativeSceneEntranceSpeedUnitsPerTick = entranceSpeed;
    link.NativeSceneEntranceTargetCaptured = true;
    link.NativeSceneEntranceTargetActorPosition = {
        actor.X + (direction.X * targetDistance), actor.Y,
        actor.Z + (direction.Z * targetDistance),
    };
    link.NativeSceneEntranceDistance = targetDistance;
    link.NativeLinearVelocityUnitsPerTick = entranceSpeed;
    link.NativeSpeedTargetUnitsPerTick = entranceSpeed;
    link.MovementYaw = link.Yaw;
    link.NativeAction = entranceSpeed > 0.0
        ? LinkNativeMovementAction::Run
        : LinkNativeMovementAction::Idle;
    return true;
}

double AdvanceNativePlayerSceneEntrance(
    LinkInstance& link, const LinkNativePlayerActionConfig& config,
    double playerTickRate, double deltaSeconds) {
    if (link.NativePlayerAction != LinkNativePlayerAction::SceneEntrance ||
        !std::isfinite(playerTickRate) || playerTickRate <= 0.0 ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        throw std::invalid_argument("invalid OOT3D player scene-entrance update");
    }

    link.NativeSceneEntranceTickAccumulator += deltaSeconds * playerTickRate;
    const int tickCount = static_cast<int>(
        std::floor(link.NativeSceneEntranceTickAccumulator));
    link.NativeSceneEntranceTickAccumulator -= static_cast<double>(tickCount);
    for (int tick = 0; tick < tickCount; ++tick) {
        if (link.NativeSceneEntranceTimer < 0) {
            ++link.NativeSceneEntranceTimer;
        }
    }

    double targetSpeed = link.NativeSceneEntranceSpeedUnitsPerTick;
    if (link.NativeSceneEntranceTimer > 0) {
        targetSpeed = config.SceneEntranceDefaultTargetSpeedUnitsPerTick;
        if (static_cast<int>(link.NativeSceneEntranceDistance) <
            config.SceneEntranceCompletionDistance) {
            targetSpeed = 0.0;
        }
    }
    link.NativeSpeedTargetUnitsPerTick = targetSpeed;
    return targetSpeed;
}

void UpdateNativePlayerSceneEntranceDistance(LinkInstance& link) {
    if (link.NativePlayerAction != LinkNativePlayerAction::SceneEntrance) {
        throw std::logic_error("OOT3D player scene entrance is not active");
    }
    link.NativeSceneEntranceDistance = SceneEntranceDistance(link);
}

bool NativePlayerSceneEntranceShouldComplete(const LinkInstance& link) {
    if (link.NativePlayerAction != LinkNativePlayerAction::SceneEntrance) {
        return false;
    }
    return link.NativeSceneEntranceTimer == 0 ||
           (static_cast<int>(link.NativeSceneEntranceDistance) == 0 &&
            link.NativeLinearVelocityUnitsPerTick == 0.0);
}

void CompleteNativePlayerSceneEntrance(
    LinkInstance& link, const LinkNativeLocomotionConfig& config) {
    if (link.NativePlayerAction != LinkNativePlayerAction::SceneEntrance) {
        throw std::logic_error("OOT3D player scene entrance completion has no active action");
    }
    if (!std::isfinite(config.RunSpeedUnitsPerTick) ||
        config.RunSpeedUnitsPerTick <= 0.0) {
        throw std::invalid_argument("invalid OOT3D locomotion speed after scene entrance");
    }
    link.NativePlayerAction = LinkNativePlayerAction::Locomotion;
    link.NativeSceneEntranceTickAccumulator = 0.0;
    link.NativeSceneEntranceSpeedUnitsPerTick = 0.0;
    link.NativeSceneEntranceTargetCaptured = false;
    link.NativeSpeedTargetUnitsPerTick = config.RunSpeedUnitsPerTick;
}

bool NativePlayerCanAutoJump(const LinkInstance& link,
                             const LinkNativeLocomotionConfig& config,
                             bool wasGrounded) {
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion ||
        !wasGrounded || link.Grounded) {
        return false;
    }
    const double floorDistance = -link.NativeFloorHeightDiff;
    return floorDistance > config.AutoJumpMinimumFloorDistance &&
           link.NativeLinearVelocityUnitsPerTick >
               config.AutoJumpMinimumSpeedUnitsPerTick &&
           PlayerMovementYawDifferenceS16(link) <
               config.AutoJumpMaximumYawDifferenceS16;
}

void BeginNativePlayerAutoJump(LinkInstance& link,
                               const LinkNativeLocomotionConfig& config,
                               double actorY) {
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion) {
        throw std::logic_error("OOT3D player auto-jump requires locomotion");
    }
    const bool runJump =
        PlayerMovementYawDifferenceS16(link) <
            config.RunAutoJumpMaximumYawDifferenceS16 &&
        link.NativeLinearVelocityUnitsPerTick >
            config.RunAutoJumpMinimumSpeedUnitsPerTick;
    const double verticalVelocity =
        link.NativeLinearVelocityUnitsPerTick >
                config.AutoJumpHighSpeedThresholdUnitsPerTick
            ? config.AutoJumpHighVerticalVelocityUnitsPerTick
            : config.AutoJumpBaseVerticalVelocityUnitsPerTick +
                  (config.AutoJumpSpeedVelocityScalePerUnit *
                   link.NativeLinearVelocityUnitsPerTick);
    BeginNativePlayerAirborne(link, actorY, verticalVelocity);
    link.NativePlayerAction = LinkNativePlayerAction::AutoJump;
    link.NativeAutoJumpKind = runJump
        ? LinkNativeAutoJumpKind::Run
        : LinkNativeAutoJumpKind::Normal;
}

bool NativePlayerIsAirborne(const LinkInstance& link) {
    return link.NativePlayerAction == LinkNativePlayerAction::Airborne ||
           link.NativePlayerAction == LinkNativePlayerAction::AutoJump;
}

bool NativePlayerShouldUseLandingAnticipation(const LinkInstance& link) {
    return NativePlayerIsAirborne(link) &&
           link.NativeVerticalVelocityUnitsPerTick < 0.0 &&
           link.NativeFallDistance > 0.0;
}

void BeginNativePlayerAirborne(LinkInstance& link, double actorY,
                               double initialVelocityYUnitsPerTick) {
    if (!std::isfinite(actorY) || !std::isfinite(initialVelocityYUnitsPerTick)) {
        throw std::invalid_argument("invalid OOT3D player airborne state");
    }
    link.NativePlayerAction = LinkNativePlayerAction::Airborne;
    link.NativeAutoJumpKind = LinkNativeAutoJumpKind::Normal;
    link.NativeVerticalVelocityUnitsPerTick = initialVelocityYUnitsPerTick;
    link.NativePhysicsTickAccumulator = 0.0;
    link.NativeFallStartY = actorY;
    link.NativeFallDistance = 0.0;
    ClearLedgeState(link);
    ClearSurfaceClimbState(link);
}

NativeVerticalMotionStep AdvanceNativePlayerAirborne(
    LinkInstance& link, const LinkNativeLocomotionConfig& config, double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
        !std::isfinite(config.PlayerTickRate) || config.PlayerTickRate <= 0.0) {
        throw std::invalid_argument("invalid OOT3D player physics time step");
    }

    link.NativeGravityUnitsPerTickSquared = config.GravityUnitsPerTickSquared;
    link.NativeMinimumVerticalVelocityUnitsPerTick =
        config.MinimumVerticalVelocityUnitsPerTick;
    link.NativePhysicsTickAccumulator += deltaSeconds * config.PlayerTickRate;
    const int tickCount = static_cast<int>(std::floor(link.NativePhysicsTickAccumulator));
    link.NativePhysicsTickAccumulator -= static_cast<double>(tickCount);

    NativeVerticalMotionStep step;
    step.TickCount = tickCount;
    for (int tick = 0; tick < tickCount; ++tick) {
        link.NativeVerticalVelocityUnitsPerTick = std::max(
            link.NativeMinimumVerticalVelocityUnitsPerTick,
            link.NativeVerticalVelocityUnitsPerTick +
                link.NativeGravityUnitsPerTickSquared);
        step.DeltaY += link.NativeVerticalVelocityUnitsPerTick;
    }
    step.VelocityYUnitsPerTick = link.NativeVerticalVelocityUnitsPerTick;
    return step;
}

void UpdateNativePlayerFallDistance(LinkInstance& link, double actorY) {
    if (!std::isfinite(actorY)) {
        throw std::invalid_argument("invalid OOT3D player actor height");
    }
    link.NativeFallDistance = std::max(
        link.NativeFallDistance, std::max(0.0, link.NativeFallStartY - actorY));
}

void BeginNativePlayerLanding(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                              double actorY) {
    const bool runAutoJump =
        link.NativePlayerAction == LinkNativePlayerAction::AutoJump &&
        link.NativeAutoJumpKind == LinkNativeAutoJumpKind::Run;
    UpdateNativePlayerFallDistance(link, actorY);
    link.NativeLandingKind = runAutoJump
        ? LinkNativeLandingKind::RunAutoJump
        : (link.NativeFallDistance <= config.ShortLandingMaximumFallDistance
               ? LinkNativeLandingKind::Short
               : LinkNativeLandingKind::Normal);
    link.NativePlayerAction = LinkNativePlayerAction::Landing;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
}

void CompleteNativePlayerLanding(LinkInstance& link) {
    link.NativePlayerAction = LinkNativePlayerAction::Locomotion;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
    link.NativeFallStartY = link.GroundY;
    link.NativeFallDistance = 0.0;
    ClearLedgeState(link);
    ClearSurfaceClimbState(link);
}

bool NativePlayerCanGrabLedge(const LinkInstance& link,
                              const LinkNativeCollisionActionConfig& config,
                              const LinkNativeLedgeQueryResult& ledge) {
    if (!ledge.Available || ledge.ClimbType < config.MinimumClimbTypeForFallingGrab) {
        return false;
    }
    if (config.FallingGrabRequiresDescending &&
        link.NativeVerticalVelocityUnitsPerTick > 0.0) {
        return false;
    }
    if (config.FallingGrabRequiresForwardSpeed &&
        link.NativeLinearVelocityUnitsPerTick <= 0.0) {
        return false;
    }
    const double ledgeHeightAboveLastFloor =
        (link.Position.Y + link.PositionToActorOffset.Y - link.GroundY) + ledge.YDistance;
    return link.NativeFallDistance < config.FallingGrabMaximumFallDistance &&
           ledgeHeightAboveLastFloor > config.FallingGrabMinimumLedgeAboveFloor;
}

void BeginNativePlayerLedgeHold(LinkInstance& link,
                               const LinkNativeLedgeQueryResult& ledge) {
    if (!ledge.Available || ledge.ClimbType <= 0) {
        throw std::invalid_argument("invalid OOT3D player ledge-hold state");
    }
    link.NativePlayerAction = LinkNativePlayerAction::LedgeHold;
    ClearSurfaceClimbState(link);
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
    link.NativeLinearVelocityUnitsPerTick = 0.0;
    link.NativeSpeedTargetUnitsPerTick = 0.0;
    link.NativeAction = LinkNativeMovementAction::Idle;
    link.Grounded = false;
    link.NativeLedgeWallPolygonIndex = ledge.WallPolygonIndex;
    link.NativeLedgeFloorPolygonIndex = ledge.LedgeFloorPolygonIndex;
    link.NativeLedgeClimbType = ledge.ClimbType;
    link.NativeLedgeYDistance = ledge.YDistance;
    link.NativeLedgeHangActorPosition = ledge.HangActorPosition;
    link.NativeLedgeTopActorPosition = ledge.TopActorPosition;
    link.NativeLedgePushNormalX = ledge.WallPushNormal.X;
    link.NativeLedgePushNormalZ = ledge.WallPushNormal.Z;
    const Vec3 wallward = {
        -ledge.WallPushNormal.X, 0.0, -ledge.WallPushNormal.Z
    };
    link.Yaw = YawFromDirection(wallward);
    link.MovementYaw = link.Yaw;
    SetActorPosition(link, ledge.HangActorPosition);
}

void BeginNativePlayerLedgeClimb(LinkInstance& link) {
    if (link.NativePlayerAction != LinkNativePlayerAction::LedgeHold ||
        link.NativeLedgeClimbType <= 0) {
        throw std::logic_error("OOT3D player cannot climb without a native ledge hold");
    }
    link.NativePlayerAction = LinkNativePlayerAction::LedgeClimb;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
}

void CompleteNativePlayerLedgeClimb(LinkInstance& link) {
    if (link.NativePlayerAction != LinkNativePlayerAction::LedgeClimb) {
        throw std::logic_error("OOT3D player ledge climb completion has no active climb");
    }
    SetActorPosition(link, link.NativeLedgeTopActorPosition);
    link.NativePlayerAction = LinkNativePlayerAction::Locomotion;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
    link.Grounded = true;
    link.GroundY = link.NativeLedgeTopActorPosition.Y;
    link.FloorPolygonIndex = link.NativeLedgeFloorPolygonIndex;
    link.NativeFallStartY = link.GroundY;
    link.NativeFallDistance = 0.0;
    ClearLedgeState(link);
}

void BeginNativePlayerSurfaceClimb(
    LinkInstance& link, const LinkNativeSurfaceClimbQueryResult& surface) {
    if (!surface.Available || surface.WallPolygonIndex < 0 ||
        surface.SurfaceMaximumY <= surface.SurfaceMinimumY ||
        surface.Mode == LinkNativeClimbMode::None) {
        throw std::invalid_argument("invalid OOT3D player surface-climb state");
    }
    ClearLedgeState(link);
    link.NativePlayerAction = LinkNativePlayerAction::SurfaceClimb;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
    link.NativeLinearVelocityUnitsPerTick = 0.0;
    link.NativeSpeedTargetUnitsPerTick = 0.0;
    link.NativeAction = LinkNativeMovementAction::Idle;
    link.Grounded = false;
    link.NativeSurfaceClimbWallPolygonIndex = surface.WallPolygonIndex;
    link.NativeClimbMode = surface.Mode;
    link.NativeSurfaceClimbWallFlags = surface.WallFlags;
    link.NativeSurfaceClimbMinimumY = surface.SurfaceMinimumY;
    link.NativeSurfaceClimbMaximumY = surface.SurfaceMaximumY;
    link.NativeSurfaceClimbAnchorActorPosition = surface.AlignedActorPosition;
    link.NativeSurfaceClimbPushNormalX = surface.WallPushNormal.X;
    link.NativeSurfaceClimbPushNormalZ = surface.WallPushNormal.Z;
    link.NativeSurfaceClimbQueryStatus =
        LinkNativeSurfaceClimbQueryStatusName(surface.Status);
    const Vec3 wallward = {
        -surface.WallPushNormal.X, 0.0, -surface.WallPushNormal.Z
    };
    link.Yaw = YawFromDirection(wallward);
    link.MovementYaw = link.Yaw;
    SetActorPosition(link, surface.AlignedActorPosition);
}

void CompleteNativePlayerSurfaceClimb(LinkInstance& link, bool grounded,
                                      double actorY) {
    if (link.NativePlayerAction != LinkNativePlayerAction::SurfaceClimb ||
        !std::isfinite(actorY)) {
        throw std::logic_error("OOT3D player surface-climb completion has no active climb");
    }
    auto actor = LinkActorPosition(link);
    actor.Y = actorY;
    SetActorPosition(link, actor);
    link.NativePlayerAction = LinkNativePlayerAction::Locomotion;
    link.NativeVerticalVelocityUnitsPerTick = 0.0;
    link.NativePhysicsTickAccumulator = 0.0;
    link.Grounded = grounded;
    if (grounded) {
        link.GroundY = actorY;
        link.NativeFallStartY = actorY;
        link.NativeFallDistance = 0.0;
    }
    ClearSurfaceClimbState(link);
}

void PopulateNativePlayerActionMotion(const LinkInstance& link, LinkMotionState& motion) {
    motion.NativePlayerAction = NativePlayerActionName(link.NativePlayerAction);
    motion.NativeSceneEntranceStartMode = link.NativeSceneEntranceStartMode;
    motion.NativeSceneEntranceTimer = link.NativeSceneEntranceTimer;
    motion.NativeSceneEntranceSpeedUnitsPerTick =
        link.NativeSceneEntranceSpeedUnitsPerTick;
    motion.NativeSceneEntranceDistance = link.NativeSceneEntranceDistance;
    motion.NativeSceneEntranceTargetCaptured =
        link.NativeSceneEntranceTargetCaptured;
    motion.NativeAutoJumpKind = link.NativePlayerAction == LinkNativePlayerAction::AutoJump
        ? NativeAutoJumpKindName(link.NativeAutoJumpKind)
        : "";
    motion.NativeLandingKind = link.NativePlayerAction == LinkNativePlayerAction::Landing
        ? NativeLandingKindName(link.NativeLandingKind)
        : "";
    motion.NativeVerticalVelocityUnitsPerTick = link.NativeVerticalVelocityUnitsPerTick;
    motion.NativeFallDistance = link.NativeFallDistance;
    motion.NativeLedgeClimbType = link.NativeLedgeClimbType;
    motion.NativeLedgeYDistance = link.NativeLedgeYDistance;
    motion.NativeLedgeQueryStatus = link.NativeLedgeQueryStatus;
    motion.NativeClimbMode = NativeClimbModeName(link.NativeClimbMode);
    motion.NativeSurfaceClimbWallFlags = link.NativeSurfaceClimbWallFlags;
    motion.NativeSurfaceClimbMinimumY = link.NativeSurfaceClimbMinimumY;
    motion.NativeSurfaceClimbMaximumY = link.NativeSurfaceClimbMaximumY;
    motion.NativeSurfaceClimbQueryStatus = link.NativeSurfaceClimbQueryStatus;
}

} // namespace Oot3dNativeGame
