#include "oot3d_native_camera_normal.h"

#include <algorithm>
#include <cmath>

#include "oot3d_demo_math.h"
#include "oot3d_link_instance.h"
#include "oot3d_native_camera_runtime.h"

Vec3 NativeCameraFocusTargetForLink(const LinkInstance& link, const NativeCameraConfig& config) {
    const auto actor = LinkActorPosition(link);
    return {
        actor.X,
        actor.Y + config.Normal0.PlayerHeight + config.Normal0.FocusYOffset,
        actor.Z,
    };
}

Vec3 NativeCameraFocusTargetForProfile(const LinkInstance& link, const NativeCameraConfig& config,
                                       const NativeCameraModeProfile& profile) {
    const auto actor = LinkActorPosition(link);
    return {
        actor.X,
        actor.Y + config.Normal0.PlayerHeight + NativeCameraScaledDistance(config, profile.DataYOffset),
        actor.Z,
    };
}

double NativeCameraSpeedRatio(const NativeCameraConfig& config, const LinkInstance& link) {
    const double denominator =
        kNativeRunSpeedUnitsPerTick * NativeCameraPercent(config.Normal0.Oreg8SpeedRatioPercent);
    if (denominator <= 0.000001) {
        return 0.0;
    }
    return std::clamp(std::abs(link.NativeLinearVelocityUnitsPerTick) / denominator, 0.0, 1.0);
}

double NativeCameraNormal1StartSwingTicks(const NativeCameraConfig& config) {
    return static_cast<double>(config.Normal0.Oreg50StartSwingHoldTicks +
                               config.Normal0.Oreg51StartSwingApproachTicks);
}

void ResetNativeNormal1AnimationState(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera) {
    camera.NativeNormal1StartSwingTimer = NativeCameraNormal1StartSwingTicks(config);
    camera.NativeNormal1SwingYawTarget = NativeCameraAtEyeYaw(camera);
    camera.NativePreviousXzSpeed = std::abs(link.NativeLinearVelocityUnitsPerTick);
    camera.NativeNormal1DistanceTarget = camera.NativeDistance;
    camera.NativeNormal1RUpdateRateTarget = config.Normal0.Oreg6RUpdateRateInv;
}

namespace {

void UpdateNativeNormal1StartSwingTimer(const NativeCameraConfig& config, const LinkInstance& link,
                                        Camera& camera, double dtTicks) {
    const double xzSpeed = std::abs(link.NativeLinearVelocityUnitsPerTick);
    if (xzSpeed > 0.001) {
        camera.NativeNormal1StartSwingTimer = NativeCameraNormal1StartSwingTicks(config);
        camera.NativeNormal1SwingYawTarget = NativeCameraPlayerBehindAtEyeYaw(link);
        return;
    }

    if (camera.NativeNormal1StartSwingTimer <= 0.0) {
        return;
    }

    if (camera.NativeNormal1StartSwingTimer > config.Normal0.Oreg50StartSwingHoldTicks) {
        const double currentAtEyeYaw = NativeCameraAtEyeYaw(camera);
        const double playerBehindYaw = NativeCameraPlayerBehindAtEyeYaw(link);
        const double yawDelta = NormalizeAngleRadians(playerBehindYaw - currentAtEyeYaw);
        camera.NativeNormal1SwingYawTarget =
            NormalizeAngleRadians(currentAtEyeYaw + (yawDelta / camera.NativeNormal1StartSwingTimer));
    }
    camera.NativeNormal1StartSwingTimer =
        std::max(0.0, camera.NativeNormal1StartSwingTimer - std::max(0.0, dtTicks));
}

double NativeCameraClampDistance(const NativeCameraConfig& config, const NativeCameraModeProfile& profile,
                                 Camera& camera, double distance, double timer, double dtTicks) {
    const double minDistance = NativeCameraScaledDistance(config, profile.DataEyeDistanceMin);
    const double maxDistance = NativeCameraScaledDistance(config, profile.DataEyeDistanceMax);
    double distanceTarget = distance;
    double rUpdateRateInvTarget = 1.0;

    if (distance < minDistance) {
        distanceTarget = minDistance;
        rUpdateRateInvTarget = std::abs(timer) > 0.000001 ? config.Normal0.Oreg6RUpdateRateInv * 0.5
                                                          : config.Normal0.Oreg6RUpdateRateInv;
    } else if (maxDistance < distance) {
        distanceTarget = maxDistance;
        rUpdateRateInvTarget = std::abs(timer) > 0.000001 ? config.Normal0.Oreg6RUpdateRateInv * 0.5
                                                          : config.Normal0.Oreg6RUpdateRateInv;
    } else {
        distanceTarget = distance;
        rUpdateRateInvTarget = std::abs(timer) > 0.000001 ? config.Normal0.Oreg6RUpdateRateInv : 1.0;
    }

    camera.NativeNormal1DistanceTarget = distanceTarget;
    camera.NativeNormal1RUpdateRateTarget = rUpdateRateInvTarget;
    camera.NativeRUpdateRateInv =
        NativeCameraLerpCeilF(rUpdateRateInvTarget, camera.NativeRUpdateRateInv,
                              NativeCameraPercent(config.Normal0.Oreg25YawRateLerpPercent), 0.1, dtTicks);
    return NativeCameraLerpCeilF(distanceTarget, camera.NativeDistance,
                                 1.0 / std::max(1.0, camera.NativeRUpdateRateInv), 0.2, dtTicks);
}

double NativeCameraDefaultYawStepScale(const NativeCameraConfig& config, const NativeCameraModeProfile& profile,
                                       const LinkInstance& link, Camera& camera, double dtTicks) {
    const double xzSpeed = std::abs(link.NativeLinearVelocityUnitsPerTick);
    const double speedRatio = NativeCameraSpeedRatio(config, link);
    const double rateLerp = speedRatio * NativeCameraPercent(config.Normal0.Oreg25YawRateLerpPercent);
    const double pitchRateLerp = speedRatio * NativeCameraPercent(config.Normal0.Oreg26PitchRateLerpPercent);
    const double rateDt = std::max(0.0, dtTicks);
    const double speedDelta = xzSpeed - camera.NativePreviousXzSpeed;
    const double accel = std::clamp(speedDelta * 0.333333333333, -1.0, 1.0);

    const int16_t currentAtEyeYaw = NativeCameraRadiansToBinang(NativeCameraAtEyeYaw(camera));
    const int16_t playerYaw = NativeCameraRadiansToBinang(link.Yaw);
    const int16_t sourceYawDelta =
        NativeCameraBinangSub(playerYaw, NativeCameraBinangRot180(currentAtEyeYaw));
    const double curveInput = xzSpeed > 0.001
                                  ? NativeCameraColPolyGetNormal(NativeCameraBinangRot180(sourceYawDelta))
                                  : NativeCameraPercent(config.Normal0.Oreg48IdleYawCurvePercent);
    const double yawCurve = NativeCameraInterpolateCurve(NativeCameraPercent(profile.DataMaxYawUpdate), curveInput);
    double yawVelocity = yawCurve + ((1.0 - yawCurve) * accel);
    if (yawVelocity < 0.0) {
        yawVelocity = 0.0;
    }
    const double velocityFactor = NativeCameraInterpolateCurve(0.5, speedRatio);
    const double yawUpdateRateTarget =
        profile.DataYawUpdateRateTarget -
        (NativeCameraPercent(config.Normal0.Oreg49YawAccelScalePercent) * profile.DataYawUpdateRateTarget * accel);

    camera.NativeSpeedRatio = speedRatio;
    camera.NativeYawUpdateRateInv =
        NativeCameraLerpCeilF(yawUpdateRateTarget, camera.NativeYawUpdateRateInv, rateLerp, 0.1, rateDt);
    camera.NativePitchUpdateRateInv =
        NativeCameraLerpCeilF(config.Normal0.Oreg7DefaultPitchUpdateRate, camera.NativePitchUpdateRateInv,
                              pitchRateLerp, 0.1, rateDt);
    camera.NativeXzOffsetUpdateRate =
        NativeCameraLerpCeilF(NativeCameraPercent(config.Normal0.Oreg2XzOffsetUpdatePercent),
                              camera.NativeXzOffsetUpdateRate, rateLerp, 0.1, rateDt);
    camera.NativeYOffsetUpdateRate =
        NativeCameraLerpCeilF(NativeCameraPercent(config.Normal0.Oreg3YOffsetUpdatePercent),
                              camera.NativeYOffsetUpdateRate, pitchRateLerp, 0.1, rateDt);
    camera.NativeFovUpdateRate =
        NativeCameraLerpCeilF(NativeCameraPercent(config.Normal0.Oreg4FovUpdatePercent),
                              camera.NativeFovUpdateRate, speedRatio * 0.05, 0.1, rateDt);
    camera.NativePreviousXzSpeed = xzSpeed;
    camera.NativeNormal1YawCurve = yawCurve;
    camera.NativeNormal1YawCurveInput = curveInput;
    camera.NativeNormal1YawVelocity = yawVelocity;
    camera.NativeNormal1YawVelocityFactor = velocityFactor;
    camera.NativeNormal1YawDelta = NativeCameraS16ToRadians(sourceYawDelta);
    camera.NativeNormal1Accel = accel;

    return std::clamp((1.0 / std::max(1.0, camera.NativeYawUpdateRateInv)) * yawVelocity * velocityFactor, 0.0, 1.0);
}

} // namespace

void ApplyNativeConfiguredNormalCamera(const NativeCameraConfig& config, const LinkInstance& link,
                                       const NativeCameraModeProfile& profile, Camera& camera, double dt) {
    const double dtTicks = std::max(0.0, dt * kOot3dNativePlayerTickRate);
    const auto actor = LinkActorPosition(link);
    const Vec3 desiredOffset = {
        0.0,
        config.Normal0.PlayerHeight + NativeCameraScaledDistance(config, profile.DataYOffset),
        0.0,
    };
    camera.NativePositionOffset =
        NativeCameraLerpCeilVec3(desiredOffset, camera.NativePositionOffset, camera.NativeYOffsetUpdateRate,
                                 camera.NativeXzOffsetUpdateRate, 0.1, dtTicks);
    const Vec3 desiredTarget = {
        actor.X + camera.NativePositionOffset.X,
        actor.Y + camera.NativePositionOffset.Y,
        actor.Z + camera.NativePositionOffset.Z,
    };
    camera.Target = NativeCameraLerpCeilVec3(desiredTarget, camera.Target, camera.NativeAtLerpStepScale,
                                             camera.NativeAtLerpStepScale, 0.2, dtTicks);

    UpdateNativeNormal1StartSwingTimer(config, link, camera, dtTicks);
    const double targetAtEyeYaw = NativeCameraPlayerBehindAtEyeYaw(link);
    const double targetYaw = NativeCameraViewYawFromAtEyeYaw(targetAtEyeYaw);
    const double xzSpeed = std::abs(link.NativeLinearVelocityUnitsPerTick);
    if (camera.NativeNormal1StartSwingTimer <= 0.0 && xzSpeed <= 0.001) {
        const double idleTargetYaw = NativeCameraViewYawFromAtEyeYaw(camera.NativeNormal1SwingYawTarget);
        const double yawDelta = NormalizeAngleRadians(idleTargetYaw - camera.Yaw);
        camera.NativeNormal1YawDelta = yawDelta;
        camera.NativeNormal1YawCurveInput = NativeCameraPercent(config.Normal0.Oreg48IdleYawCurvePercent);
        camera.NativeNormal1YawCurve = 0.0;
        camera.NativeNormal1YawVelocity = 0.0;
        camera.NativeNormal1YawVelocityFactor = 0.0;
        camera.NativeNormal1Accel = 0.0;
        camera.Yaw = NormalizeAngleRadians(
            camera.Yaw +
            (yawDelta * SmoothScaleForTicks(1.0 / std::max(1.0, camera.NativeYawUpdateRateInv), dtTicks)));
    } else {
        const double yawStepScale = NativeCameraDefaultYawStepScale(config, profile, link, camera, dtTicks);
        const double yawDelta = NormalizeAngleRadians(targetYaw - camera.Yaw);
        camera.Yaw = NormalizeAngleRadians(camera.Yaw + (yawDelta * SmoothScaleForTicks(yawStepScale, dtTicks)));
    }
    camera.NativeNormal1SwingYawTarget = targetAtEyeYaw;

    camera.Pitch = NativeCameraLerpCeilF(-DegreesToRadians(profile.DataPitchTargetDegrees), camera.Pitch,
                                         1.0 / std::max(1.0, camera.NativePitchUpdateRateInv), 0.0001, dtTicks);
    camera.Pitch = std::clamp(camera.Pitch,
                              -static_cast<double>(config.Normal0.Oreg5MaxPitchS16) * kOot3dS16AngleToRadians,
                              static_cast<double>(config.Normal0.Oreg5MaxPitchS16) * kOot3dS16AngleToRadians);
    camera.FovDegrees = NativeCameraLerpCeilF(profile.DataFovDegrees, camera.FovDegrees,
                                              camera.NativeFovUpdateRate, 1.0, dtTicks);
    camera.NativeDistance =
        NativeCameraClampDistance(config, profile, camera, camera.NativeDistance,
                                  camera.NativeNormal1StartSwingTimer, dtTicks);
    camera.Position = Subtract(camera.Target, Scale(CameraForward(camera), camera.NativeDistance));
    camera.NativeCameraActive = true;
    camera.NativeBehavior = profile.Behavior;
    camera.NativeSet = profile.Set;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = profile.Function;
    camera.NativeSceneCameraApplied = false;
    camera.NativeStartSceneCameraApplied = false;
    camera.NativeAtLerpStepScale = NativeCameraClampAtLerpScale(config, camera.NativeAtLerpStepScale,
                                                                profile.DataAtLerpStepScale);
    camera.NativeYawUpdateRateTarget = profile.DataYawUpdateRateTarget;
    camera.NativePrerendPivotYawInitialized = false;
    SyncNativeCameraInputDirection(camera);
}

void InitializeNativeNormal0Camera(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera) {
    camera.Target = NativeCameraFocusTargetForLink(link, config);
    camera.Yaw = NativeCameraPlayerBehindViewYaw(link);
    camera.Pitch = -NativeCameraS16ToRadians(config.Normal0.InitialPitchS16);
    camera.FovDegrees = config.Normal0.DataFovDegrees;
    camera.NativeDistance = config.Normal0.InitialEyeDistance;
    const auto actor = LinkActorPosition(link);
    camera.NativePositionOffset = {
        camera.Target.X - actor.X,
        camera.Target.Y - actor.Y,
        camera.Target.Z - actor.Z,
    };
    camera.NativeYawUpdateRateInv = config.Normal0.DataYawUpdateRateTarget;
    camera.NativePitchUpdateRateInv = config.Normal0.Oreg7DefaultPitchUpdateRate;
    camera.NativeRUpdateRateInv = config.Normal0.Oreg6RUpdateRateInv;
    camera.NativeXzOffsetUpdateRate = NativeCameraPercent(config.Normal0.Oreg2XzOffsetUpdatePercent);
    camera.NativeYOffsetUpdateRate = NativeCameraPercent(config.Normal0.Oreg3YOffsetUpdatePercent);
    camera.NativeFovUpdateRate = NativeCameraPercent(config.Normal0.Oreg4FovUpdatePercent);
    camera.NativeSpeedRatio = 0.0;
    camera.NativePreviousXzSpeed = std::abs(link.NativeLinearVelocityUnitsPerTick);
    camera.NativeNormal1YawVelocity = 0.0;
    camera.NativeNormal1YawCurve = 0.0;
    camera.NativeNormal1YawCurveInput = 0.0;
    camera.NativeNormal1YawVelocityFactor = 0.0;
    camera.NativeNormal1YawDelta = 0.0;
    camera.NativeNormal1Accel = 0.0;
    camera.NativeNormal1StartSwingTimer = NativeCameraNormal1StartSwingTicks(config);
    camera.NativeNormal1SwingYawTarget = NativeCameraAtEyeYaw(camera);
    camera.NativeNormal1DistanceTarget = camera.NativeDistance;
    camera.NativeNormal1RUpdateRateTarget = config.Normal0.Oreg6RUpdateRateInv;
    camera.Position = Subtract(camera.Target, Scale(CameraForward(camera), camera.NativeDistance));
    camera.MoveSpeed = std::max(60.0, config.Normal0.EyeDistanceMax);
    camera.NativeCameraActive = true;
    camera.NativeBehavior = "normal_follow";
    camera.NativeSet = config.Normal0.Set;
    camera.NativeMode = config.Normal0.Mode;
    camera.NativeFunction = config.Normal0.Function;
    camera.NativeSceneCameraApplied = false;
    camera.NativeAtLerpStepScale = config.Normal0.DataAtLerpStepScale;
    camera.NativeYawUpdateRateTarget = config.Normal0.DataYawUpdateRateTarget;
    camera.NativeCollisionSupported = false;
    camera.NativeCollisionPolygonFlagsSupported = config.CollisionPolygonFlagsSupported;
    camera.NativeCollisionActive = false;
    camera.NativeCollisionPolygonIndex = -1;
    camera.NativeCollisionPreviousPolygonIndex = -1;
    camera.NativeCollisionDisplacement = 0.0;
    camera.NativeCollisionIgnoredCameraPolygonCount = 0;
    camera.NativeCollisionRetainedPreviousPolygon = false;
    SyncNativeCameraInputDirection(camera);
}

void UpdateNativeNormal0Camera(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera,
                               double dt) {
    ApplyNativeConfiguredNormalCamera(config, link, NativeCameraModeProfileForSetting(config, kNativeCameraSetNormal0),
                                      camera, dt);
}
