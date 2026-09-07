#include "oot3d_link_movement.h"

#include <algorithm>
#include <cmath>

#include "oot3d_demo_math.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_collision.h"

LinkMotionState ApplyLinkMovementIntent(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                                        const Vec3& direction, double speed, double dt) {
    LinkMotionState motion;
    const double dtTicks = std::max(0.0, dt * config.PlayerTickRate);
    const bool hasInput = Dot(direction, direction) > 0.000001 && speed > 0.0;
    motion.HasInput = hasInput;
    const double speedScale = link.MoveSpeed > 0.000001 ? speed / link.MoveSpeed : 1.0;
    const double uncappedTargetSpeedUnitsPerTick = hasInput ? link.NativeSpeedTargetUnitsPerTick * speedScale : 0.0;
    const double targetSpeedUnitsPerTick =
        hasInput ? std::min(uncappedTargetSpeedUnitsPerTick, link.NativeWallSpeedLimitUnitsPerTick) : 0.0;
    const double shapeYawStepRadians = link.ShapeYawStepS16PerTick * kOot3dS16AngleToRadians * dtTicks;
    const bool wasIdle = link.NativeAction == LinkNativeMovementAction::Idle &&
                         std::abs(link.NativeLinearVelocityUnitsPerTick) <= 0.000001;
    double velocityTargetUnitsPerTick = targetSpeedUnitsPerTick;
    double velocityIncrementStepUnitsPerTick = link.NativeLinearAccelUnitsPerTick;
    double velocityDecrementStepUnitsPerTick = link.NativeLinearDecelUnitsPerTick;

    if (hasInput) {
        motion.Direction = Normalize(direction);
        motion.TargetYaw = YawFromDirection(motion.Direction);
        motion.YawDelta = NormalizeAngleRadians(motion.TargetYaw - link.MovementYaw);

        if (wasIdle) {
            link.MovementYaw = motion.TargetYaw;
            link.Yaw = motion.TargetYaw;
            link.NativeAction = LinkNativeMovementAction::Run;
            motion.YawStepRadians = std::abs(motion.YawDelta);
            motion.YawAlignmentMode = "idle_speedTarget_nonzero_snap_func_8083C8DC";
        } else if (std::abs(motion.YawDelta) > config.LargeYawBrakeThresholdRadians &&
                   std::abs(link.NativeLinearVelocityUnitsPerTick) > 0.000001) {
            velocityTargetUnitsPerTick = 0.0;
            velocityIncrementStepUnitsPerTick = link.NativeDecelerateToZeroUnitsPerTick;
            velocityDecrementStepUnitsPerTick = link.NativeDecelerateToZeroUnitsPerTick;
            motion.LargeYawBrake = true;
            motion.YawAlignmentMode = "run_large_yaw_brake_func_8083C484";
        } else {
            motion.YawStepRadians = link.MovingYawStepS16PerTick * kOot3dS16AngleToRadians * dtTicks;
            link.MovementYaw = MoveAngleToward(link.MovementYaw, motion.TargetYaw, motion.YawStepRadians);
            link.Yaw = MoveAngleToward(link.Yaw, link.MovementYaw, shapeYawStepRadians);
            link.NativeAction = LinkNativeMovementAction::Run;
            motion.YawAlignmentMode = "run_func_8083DF68_REG27_scaled_step";
        }
    } else {
        link.Yaw = MoveAngleToward(link.Yaw, link.MovementYaw, shapeYawStepRadians);
        motion.YawAlignmentMode = "no_input_decelerate_to_idle";
    }

    link.NativeLinearVelocityUnitsPerTick =
        AsymStepToward(link.NativeLinearVelocityUnitsPerTick, velocityTargetUnitsPerTick,
                       velocityIncrementStepUnitsPerTick * dtTicks, velocityDecrementStepUnitsPerTick * dtTicks);
    if (!hasInput && std::abs(link.NativeLinearVelocityUnitsPerTick) <= 0.000001) {
        link.NativeAction = LinkNativeMovementAction::Idle;
    }
    if (motion.LargeYawBrake && std::abs(link.NativeLinearVelocityUnitsPerTick) <= 0.000001) {
        link.NativeAction = LinkNativeMovementAction::Idle;
    }

    motion.NativeAction = NativeMovementActionName(link.NativeAction);
    motion.NativeSpeedTargetUnitsPerTick = velocityTargetUnitsPerTick;
    motion.NativeLinearVelocityUnitsPerTick = link.NativeLinearVelocityUnitsPerTick;
    motion.NativeVelocityStepUnitsPerTick = velocityTargetUnitsPerTick >= link.NativeLinearVelocityUnitsPerTick
                                                ? velocityIncrementStepUnitsPerTick
                                                : velocityDecrementStepUnitsPerTick;
    motion.NativeWallSpeedLimitUnitsPerTick = link.NativeWallSpeedLimitUnitsPerTick;
    motion.NativeWallSpeedLimitActive = link.NativeWallSpeedLimitActive;
    motion.Speed = link.NativeLinearVelocityUnitsPerTick * config.PlayerTickRate;
    motion.SpeedRatio = link.MoveSpeed > 0.000001 ? motion.Speed / link.MoveSpeed : 1.0;
    motion.MovementYaw = link.MovementYaw;
    motion.ShapeYaw = link.Yaw;
    motion.MovementDirection = DirectionFromYaw(link.MovementYaw);
    motion.Moving = motion.Speed > 0.000001;
    if (motion.Moving) {
        AddScaled(link.Position, motion.MovementDirection, motion.Speed * dt);
    }
    UpdateLinkMotionLocomotionAnimationClass(motion, link, config);
    return motion;
}

LinkMotionState ApplyLinkMovementIntentWithCollision(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                                     const LinkNativeLocomotionConfig& config,
                                                     LinkInstance& link, const Vec3& direction, double speed,
                                                     double dt) {
    const auto startPosition = link.Position;
    LinkMotionState motion = ApplyLinkMovementIntent(link, config, direction, speed, dt);
    const auto targetPosition = link.Position;
    link.Position = startPosition;
    ApplyLinkCollisionConstrainedTranslation(scene, config, link,
                                             { targetPosition.X - startPosition.X,
                                               targetPosition.Y - startPosition.Y,
                                               targetPosition.Z - startPosition.Z });
    UpdateLinkMotionLocomotionAnimationClass(motion, link, config);
    return motion;
}
