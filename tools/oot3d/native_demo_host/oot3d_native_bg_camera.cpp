#include "oot3d_native_bg_camera.h"

#include <algorithm>
#include <cmath>

#include "oot3d_demo_math.h"
#include "oot3d_link_instance.h"
#include "oot3d_native_camera_collision.h"
#include "oot3d_native_camera_normal.h"
#include "oot3d_native_camera_runtime.h"
#include "oot3d_native_camera_scene.h"

namespace {

double NativeBgCameraFovDegrees(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCameraPosition& position,
                                const NativeCameraConfig& config) {
    const double raw = position.Other.X;
    if (raw <= 0.0) {
        return config.Normal0.DataFovDegrees;
    }
    return std::abs(raw) > 360.0 ? raw / 100.0 : raw;
}

Vec3 NativeCameraForwardFromBgRotation(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& rotation) {
    Camera basis;
    basis.Yaw = NativeCameraS16ToRadians(rotation.Y);
    basis.Pitch = -NativeCameraS16ToRadians(rotation.X);
    return CameraForward(basis);
}

Vec3 ClosestPointOnLine(const Vec3& linePoint, const Vec3& lineDirection, const Vec3& point) {
    const Vec3 direction = Normalize(lineDirection);
    const Vec3 pointDelta = Subtract(point, linePoint);
    return Add(linePoint, Scale(direction, Dot(pointDelta, direction)));
}

void ResetNativeCameraCollision(Camera& camera, const NativeCameraConfig& config) {
    camera.NativeCollisionSupported = false;
    camera.NativeCollisionPolygonFlagsSupported = config.CollisionPolygonFlagsSupported;
    camera.NativeCollisionActive = false;
    camera.NativeCollisionPreviousPolygonIndex = camera.NativeCollisionPolygonIndex;
    camera.NativeCollisionPolygonIndex = -1;
    camera.NativeCollisionHitPosition = {};
    camera.NativeCollisionNormal = {};
    camera.NativeCollisionDisplacement = 0.0;
    camera.NativeCollisionIgnoredCameraPolygonCount = 0;
    camera.NativeCollisionRetainedPreviousPolygon = false;
}

} // namespace

bool ApplyNativeBgCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                         const LinkInstance& link, int cameraDataIndex,
                         const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& bgCamera, Camera& camera, double dt) {
    const auto* cameraPosition = NativeBgCameraPositionForCamera(scene, bgCamera);
    if (cameraPosition == nullptr && !NativeCameraSettingIsNormalFollowFamily(bgCamera.Setting)) {
        return false;
    }

    const bool sameCameraData = camera.NativeSceneCameraDataIndex == cameraDataIndex &&
                                camera.NativeSceneCameraSetting == bgCamera.Setting;
    camera.NativeCameraActive = true;
    camera.NativeSet = bgCamera.Setting;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = NativeCameraFunctionForSetting(config, bgCamera.Setting);
    camera.NativeSceneCameraDataIndex = cameraDataIndex;
    camera.NativeSceneCameraSetting = bgCamera.Setting;
    camera.NativeSceneCameraPositionIndex = bgCamera.CameraPositionVectorIndex;
    camera.NativeSceneCameraApplied = !NativeCameraSettingIsNormalFollowFamily(bgCamera.Setting);
    camera.NativeStartSceneCameraApplied = cameraDataIndex == config.StartCameraDataIndex;

    if (NativeCameraSettingIsNormalFollowFamily(bgCamera.Setting)) {
        if (!sameCameraData) {
            ResetNativeNormal1AnimationState(config, link, camera);
        }
        ApplyNativeConfiguredNormalCamera(config, link, NativeCameraModeProfileForSetting(config, bgCamera.Setting),
                                          camera, dt);
        camera.NativeSceneCameraDataIndex = cameraDataIndex;
        camera.NativeSceneCameraSetting = bgCamera.Setting;
        camera.NativeSceneCameraPositionIndex = bgCamera.CameraPositionVectorIndex;
        camera.NativeStartSceneCameraApplied = cameraDataIndex == config.StartCameraDataIndex;
        ApplyNativeCameraCollision(scene, config, camera);
        return true;
    }

    const double dtTicks = std::max(0.0, dt * kOot3dNativePlayerTickRate);
    const Vec3 playerFocus = NativeCameraFocusTargetForLink(link, config);
    const Vec3 bgPosition = ToVec3(cameraPosition->Position);
    camera.FovDegrees = NativeBgCameraFovDegrees(*cameraPosition, config);
    ResetNativeCameraCollision(camera, config);

    if (bgCamera.Setting == kNativeCameraSetPrerendFixed) {
        camera.NativeBehavior = "bgcam_prerend_fixed_fixd3";
        camera.Position = bgPosition;
        camera.Yaw = NativeCameraS16ToRadians(cameraPosition->Rotation.Y);
        camera.Pitch = -NativeCameraS16ToRadians(cameraPosition->Rotation.X);
        camera.Target = Add(camera.Position, Scale(CameraForward(camera), config.PrerendFixedTargetRadius));
        camera.NativePrerendPivotYawInitialized = false;
        SyncNativeCameraInputDirection(camera);
        return true;
    }

    if (bgCamera.Setting == kNativeCameraSetPrerendPivot) {
        camera.NativeBehavior = "bgcam_prerend_pivot_uniq7";
        camera.Position = bgPosition;
        const Vec3 playerPosition = ToVec3(LinkActorPosition(link));
        const Vec3 eyeToPlayer = Subtract(playerPosition, camera.Position);
        const double distance = std::sqrt(Dot(eyeToPlayer, eyeToPlayer));
        const double desiredYaw = NormalizeAngleRadians(std::atan2(eyeToPlayer.X, -eyeToPlayer.Z));
        if (!sameCameraData || !camera.NativePrerendPivotYawInitialized) {
            camera.NativePrerendPivotYaw = desiredYaw;
            camera.NativePrerendPivotYawInitialized = true;
        } else {
            const double dtTicks = std::max(0.0, dt * kOot3dNativePlayerTickRate);
            const double yawScale = SmoothScaleForTicks(config.PrerendPivotYawLerp, dtTicks);
            const double yawDelta = NormalizeAngleRadians(desiredYaw - camera.NativePrerendPivotYaw);
            const double maxYawDelta =
                config.PrerendPivotMaxYawStepS16 * kOot3dS16AngleToRadians * dtTicks;
            camera.NativePrerendPivotYaw = NormalizeAngleRadians(
                camera.NativePrerendPivotYaw +
                std::clamp(yawDelta * yawScale, -maxYawDelta, maxYawDelta));
        }

        const double pitchBase = -NativeCameraS16ToRadians(cameraPosition->Rotation.X);
        const double yawReference = NativeCameraS16ToRadians(cameraPosition->Rotation.Y);
        camera.Yaw = camera.NativePrerendPivotYaw;
        camera.Pitch = pitchBase * std::cos(NormalizeAngleRadians(camera.Yaw - yawReference));
        camera.FovDegrees = kNativeCameraNormal0FovDeg;
        camera.Target = Add(camera.Position, Scale(CameraForward(camera), std::max(1.0, distance)));
        SyncNativeCameraInputDirection(camera);
        return true;
    }

    if (bgCamera.Setting == kNativeCameraSetPivotCrawlspace) {
        camera.NativeBehavior = "bgcam_pivot_crawlspace_fixd2";
        if (!sameCameraData) {
            camera.Position = bgPosition;
            camera.Target = playerFocus;
        } else {
            camera.Position = SmoothStepToTarget(camera.Position, bgPosition, 0.50, dtTicks);
            camera.Target = SmoothStepToTarget(camera.Target, playerFocus, 0.80, dtTicks);
        }
        LookAt(camera, camera.Target);
        return true;
    }

    if (bgCamera.Setting == kNativeCameraSetPivotInFront) {
        camera.NativeBehavior = "bgcam_pivot_in_front_fixd4";
        if (!sameCameraData) {
            camera.Position = bgPosition;
        } else {
            camera.Position = SmoothStepToTarget(camera.Position, bgPosition, 0.50, dtTicks);
        }
        camera.Target = SmoothStepToTarget(camera.Target, playerFocus, 0.80, dtTicks);
        LookAt(camera, camera.Target);
        return true;
    }

    if (bgCamera.Setting == kNativeCameraSetStart1) {
        camera.NativeBehavior = "bgcam_start1_unique0";
        const auto actor = LinkActorPosition(link);
        if (!sameCameraData || !camera.NativeUnique0Initialized) {
            camera.NativeUnique0Initialized = true;
            camera.NativeUnique0Timer = cameraPosition->Other.Y == -1.0 ? 0.0 : std::max(0.0, cameraPosition->Other.Y);
            camera.NativeUnique0InitialActor = actor;
            camera.Position = bgPosition;
        }

        const double movedDistanceSq = DistanceSqXZ(actor, camera.NativeUnique0InitialActor);
        if (camera.NativeUnique0Timer > 0.0) {
            camera.NativeUnique0Timer = std::max(0.0, camera.NativeUnique0Timer - dtTicks);
            camera.NativeUnique0InitialActor = actor;
        } else if (movedDistanceSq >= config.Unique0ExitDistance * config.Unique0ExitDistance ||
                   link.NativeLinearVelocityUnitsPerTick > 0.001) {
            camera.NativeSuppressedCameraDataIndex = cameraDataIndex;
            camera.NativeSuppressedCameraSetting = bgCamera.Setting;
            camera.NativeUnique0Initialized = false;
            return false;
        }

        camera.Position = bgPosition;
        const Vec3 lineDirection = NativeCameraForwardFromBgRotation(cameraPosition->Rotation);
        camera.Target = ClosestPointOnLine(camera.Position, lineDirection, playerFocus);
        LookAt(camera, camera.Target);
        return true;
    }

    if (bgCamera.Setting == kNativeCameraSetCrawlspace) {
        const auto* railEnd = NativeBgCameraPositionForCameraGroup(scene, bgCamera, 1);
        if (railEnd == nullptr) {
            return false;
        }
        camera.NativeBehavior = "bgcam_crawlspace_subj4";
        const Vec3 railStartPosition = bgPosition;
        const Vec3 railEndPosition = ToVec3(railEnd->Position);
        const Vec3 railDirection = Subtract(railEndPosition, railStartPosition);
        camera.Position = ClosestPointOnLine(railStartPosition, railDirection, ToVec3(LinkActorPosition(link)));
        camera.Target = Add(camera.Position, Normalize(railDirection));
        LookAt(camera, camera.Target);
        return true;
    }

    return false;
}
