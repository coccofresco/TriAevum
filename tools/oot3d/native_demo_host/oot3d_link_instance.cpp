#include "oot3d_link_instance.h"

#include "oot3d_link_collision.h"
#include "oot3d_native_camera_runtime.h"

LinkInstance InitialLinkInstance(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                 const LinkNativeLocomotionConfig& config) {
    LinkInstance link;
    link.Position = scene.LinkOffset;
    link.PositionToActorOffset = {
        scene.Spawn.X - link.Position.X,
        scene.Spawn.Y - link.Position.Y,
        scene.Spawn.Z - link.Position.Z,
    };
    link.Scale = scene.LinkScale;
    link.MoveSpeed = config.RunSpeedUnitsPerSecond;
    link.NativeSpeedTargetUnitsPerTick = config.RunSpeedUnitsPerTick;
    link.NativeLinearAccelUnitsPerTick = config.LinearAccelUnitsPerTick;
    link.NativeLinearDecelUnitsPerTick = config.LinearDecelUnitsPerTick;
    link.NativeDecelerateToZeroUnitsPerTick = config.DecelerateToZeroUnitsPerTick;
    link.MovingYawStepS16PerTick = config.MovingYawStepS16PerTick;
    link.ShapeYawStepS16PerTick = config.ShapeYawStepS16PerTick;
    link.NativeGravityUnitsPerTickSquared = config.GravityUnitsPerTickSquared;
    link.NativeMinimumVerticalVelocityUnitsPerTick = config.MinimumVerticalVelocityUnitsPerTick;
    link.NativeWallSpeedLimitUnitsPerTick = config.RunSpeedUnitsPerTick;
    link.ColliderRadius = scene.LinkRadius;
    link.ColliderHeight = scene.LinkTargetHeight;
    if (scene.PlayerStart.Valid) {
        link.Yaw = NativeCameraS16ToRadians(scene.PlayerStart.Rotation.Y);
        link.MovementYaw = link.Yaw;
    }
    ApplyLinkFloorGrounding(scene, link);
    link.NativeFallStartY = LinkActorPosition(link).Y;
    return link;
}

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 LinkActorPosition(const LinkInstance& link) {
    return {
        link.Position.X + link.PositionToActorOffset.X,
        link.Position.Y + link.PositionToActorOffset.Y,
        link.Position.Z + link.PositionToActorOffset.Z,
    };
}

void SetLinkActorPosition(LinkInstance& link, const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actorPosition) {
    link.Position = {
        actorPosition.X - link.PositionToActorOffset.X,
        actorPosition.Y - link.PositionToActorOffset.Y,
        actorPosition.Z - link.PositionToActorOffset.Z,
    };
}

double DistanceSqXZ(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& left,
                    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& right) {
    const double dx = left.X - right.X;
    const double dz = left.Z - right.Z;
    return dx * dx + dz * dz;
}
