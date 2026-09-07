#pragma once

#include <map>
#include <string>
#include <vector>

#include "oot3d_demo_host_types.h"

struct NativeCameraModeProfile {
    int Set = kNativeCameraSetNormal0;
    int Mode = kNativeCameraModeNormal;
    int Function = kNativeCameraFuncNorm1;
    std::string Behavior = "normal_follow";
    double DataYOffset = kNativeCameraNormal0YOffset;
    double DataEyeDistanceMin = kNativeCameraNormal0EyeDistMin;
    double DataEyeDistanceMax = kNativeCameraNormal0EyeDistMax;
    double DataPitchTargetDegrees = kNativeCameraNormal0PitchTargetDeg;
    double DataYawUpdateRateTarget = kNativeCameraNormal0YawUpdateRateTarget;
    double DataMaxYawUpdate = kNativeCameraNormal0MaxYawUpdate;
    double DataFovDegrees = kNativeCameraNormal0FovDeg;
    double DataAtLerpStepScale = kNativeCameraNormal0AtLerpStepScale;
};

struct NativeCameraNormal0Config {
    int Set = kNativeCameraSetNormal0;
    int Mode = kNativeCameraModeNormal;
    int Function = kNativeCameraFuncNormal1;
    double DataYOffset = kNativeCameraNormal0YOffset;
    double DataEyeDistanceMin = kNativeCameraNormal0EyeDistMin;
    double DataEyeDistanceMax = kNativeCameraNormal0EyeDistMax;
    double DataPitchTargetDegrees = kNativeCameraNormal0PitchTargetDeg;
    double DataYawUpdateRateTarget = kNativeCameraNormal0YawUpdateRateTarget;
    double DataMaxYawUpdate = kNativeCameraNormal0MaxYawUpdate;
    double DataFovDegrees = kNativeCameraNormal0FovDeg;
    double DataAtLerpStepScale = kNativeCameraNormal0AtLerpStepScale;
    double InitialDistance = kNativeCameraInitialDistance;
    int InitialPitchS16 = kNativeCameraInitialPitchS16;
    int Oreg5MaxPitchS16 = kNativeCameraOreg5MaxPitchS16;
    int Oreg6RUpdateRateInv = kNativeCameraOreg6RUpdateRateInv;
    int Oreg2XzOffsetUpdatePercent = kNativeCameraOreg2XzOffsetUpdatePercent;
    int Oreg3YOffsetUpdatePercent = kNativeCameraOreg3YOffsetUpdatePercent;
    int Oreg4FovUpdatePercent = kNativeCameraOreg4FovUpdatePercent;
    int Oreg7DefaultPitchUpdateRate = kNativeCameraOreg7DefaultPitchUpdateRate;
    int Oreg8SpeedRatioPercent = kNativeCameraOreg8SpeedRatioPercent;
    int Oreg25YawRateLerpPercent = kNativeCameraOreg25YawRateLerpPercent;
    int Oreg26PitchRateLerpPercent = kNativeCameraOreg26PitchRateLerpPercent;
    int Oreg41AtLerpMinPercent = kNativeCameraOreg41AtLerpMinPercent;
    int Oreg42AtLerpScalePercent = kNativeCameraOreg42AtLerpScalePercent;
    int Oreg46YOffsetNorm = kNativeCameraOreg46YOffsetNorm;
    int Oreg48IdleYawCurvePercent = kNativeCameraOreg48IdleYawCurvePercent;
    int Oreg49YawAccelScalePercent = kNativeCameraOreg49YawAccelScalePercent;
    int Oreg50StartSwingHoldTicks = kNativeCameraOreg50StartSwingHoldTicks;
    int Oreg51StartSwingApproachTicks = kNativeCameraOreg51StartSwingApproachTicks;
    double ReferencePlayerHeight = kNativeCameraReferencePlayerHeight;
    double PlayerHeight = 0.0;
    double HeightNorm = 1.0;
    double CameraUnit = 1.0;
    double FocusYOffset = kNativeCameraNormal0YOffset;
    double InitialEyeDistance = kNativeCameraInitialDistance;
    double EyeDistanceMin = kNativeCameraNormal0EyeDistMin;
    double EyeDistanceMax = kNativeCameraNormal0EyeDistMax;
};

struct NativeCameraConfig {
    NativeCameraNormal0Config Normal0;
    bool CollisionCameraDataSupported = false;
    bool CollisionLineTestSupported = false;
    bool CollisionPolygonFlagsSupported = false;
    bool StartCameraDataSupported = false;
    int StartCameraDataIndex = -1;
    double CollisionRayExtend = kNativeCameraCollisionRayExtend;
    double CollisionSurfacePush = kNativeCameraCollisionSurfacePush;
    double PrerendFixedTargetRadius = kNativeCameraPrerendFixedTargetRadius;
    double PrerendPivotYawLerp = kNativeCameraPrerendPivotYawLerp;
    double PrerendPivotMaxYawStepS16 = kNativeCameraPrerendPivotMaxYawStepS16;
    double Unique0ExitDistance = kNativeCameraUnique0ExitDistance;
    bool FloorNoneDefaultCameraSelectorSupported = false;
    std::string FloorNoneDefaultCameraSelector;
    std::vector<int> FloorNoneDefaultCameraSettings;
    std::vector<int> FloorNoneDefaultStartSettingsThatYield;
    bool NativeCameraTableAvailable = false;
    bool NativeCameraTableCompiledFallbackUsed = true;
    bool NativeCameraTableRepoRootDefaultUsed = false;
    std::string Source = "compiled_fallback_and_scene_collision_bgcam";
    std::string NativeCameraTablePath;
    std::string NativeCameraTableFormat;
    std::string NativeCameraTableSourceKind;
    std::vector<std::string> NativeCameraTableMissingFields;
    std::map<int, int> FunctionBySetting;
    std::map<int, NativeCameraModeProfile> ModeProfiles;
};

NativeCameraModeProfile NativeCameraCompiledModeProfileForSetting(int setting);
int NativeCameraCompiledFunctionForSetting(int setting);
bool NativeCameraSettingIsNormalFollowFamily(int setting);
NativeCameraModeProfile NativeCameraModeProfileForSetting(const NativeCameraConfig& config, int setting);
int NativeCameraFunctionForSetting(const NativeCameraConfig& config, int setting);
double NativeCameraScaledDistance(const NativeCameraConfig& config, double cameraDataValue);
NativeCameraConfig BuildNativeCameraConfig(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
bool NativeCameraConfigSupported(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                 const NativeCameraConfig& config);
