#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "oot3d_intro_cutscene_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

class Oot3dDemoHostPlayerController;
class Oot3dDemoHostActorRuntime;

constexpr int kNativeCameraSetNone = 0;
constexpr int kNativeCameraSetNormal0 = 1;
constexpr int kNativeCameraSetDungeon0 = 3;
constexpr int kNativeCameraSetNormal3 = 5;
constexpr int kNativeCameraSetTowerClimb = 18;
constexpr int kNativeCameraSetPivotCrawlspace = 22;
constexpr int kNativeCameraSetPivotInFront = 24;
constexpr int kNativeCameraModeNormal = 0;
constexpr int kNativeCameraFuncNorm1 = 2;
constexpr int kNativeCameraFuncNorm2 = 3;
constexpr int kNativeCameraFuncJump3 = 24;
constexpr int kNativeCameraFuncFixed2 = 33;
constexpr int kNativeCameraFuncFixed3 = 34;
constexpr int kNativeCameraFuncFixed4 = 35;
constexpr int kNativeCameraFuncSubj4 = 20;
constexpr int kNativeCameraFuncUnique0 = 41;
constexpr int kNativeCameraFuncUnique7 = 48;
constexpr int kNativeCameraFuncNormal1 = 2;
constexpr int kNativeCameraSetPrerendFixed = 25;
constexpr int kNativeCameraSetPrerendPivot = 26;
constexpr int kNativeCameraSetCrawlspace = 30;
constexpr int kNativeCameraSetStart1 = 32;
constexpr double kNativeCameraNormal0YOffset = -20.0;
constexpr double kNativeCameraNormal0EyeDistMin = 200.0;
constexpr double kNativeCameraNormal0EyeDistMax = 300.0;
constexpr double kNativeCameraNormal0PitchTargetDeg = 10.0;
constexpr double kNativeCameraNormal0YawUpdateRateTarget = 12.0;
constexpr double kNativeCameraNormal0MaxYawUpdate = 35.0;
constexpr double kNativeCameraNormal0FovDeg = 60.0;
constexpr double kNativeCameraNormal0AtLerpStepScale = 0.60;
constexpr double kNativeCameraInitialDistance = 180.0;
constexpr int kNativeCameraInitialPitchS16 = 0x071C;
constexpr int kNativeCameraOreg5MaxPitchS16 = 14500;
constexpr int kNativeCameraOreg6RUpdateRateInv = 20;
constexpr int kNativeCameraOreg2XzOffsetUpdatePercent = 5;
constexpr int kNativeCameraOreg3YOffsetUpdatePercent = 5;
constexpr int kNativeCameraOreg4FovUpdatePercent = 5;
constexpr int kNativeCameraOreg7DefaultPitchUpdateRate = 16;
constexpr int kNativeCameraOreg8SpeedRatioPercent = 150;
constexpr int kNativeCameraOreg25YawRateLerpPercent = 50;
constexpr int kNativeCameraOreg26PitchRateLerpPercent = 20;
constexpr int kNativeCameraOreg41AtLerpMinPercent = 12;
constexpr int kNativeCameraOreg42AtLerpScalePercent = 110;
constexpr int kNativeCameraOreg46YOffsetNorm = -10;
constexpr int kNativeCameraOreg48IdleYawCurvePercent = 30;
constexpr int kNativeCameraOreg49YawAccelScalePercent = 70;
constexpr int kNativeCameraOreg50StartSwingHoldTicks = 20;
constexpr int kNativeCameraOreg51StartSwingApproachTicks = 20;
constexpr double kNativeCameraReferencePlayerHeight = 68.0;
constexpr double kNativeCameraCollisionRayExtend = 8.0;
constexpr double kNativeCameraCollisionSurfacePush = 1.0;
constexpr int kNativeCameraIgnoreSentinel = 0xFF;
constexpr int kNativeCameraPrerendFixedTargetRadius = 150;
constexpr double kNativeCameraPrerendPivotYawLerp = 0.4;
constexpr double kNativeCameraPrerendPivotMaxYawStepS16 = 0x7D0;
constexpr double kNativeCameraUnique0ExitDistance = 10.0;
constexpr int kNativeColPolyIgnoreCameraRawFlag = 0x2000;
constexpr const char* kNativeCameraCollisionDataBasis =
    "OOT3D ZSI CollisionHeader bgCam table: SurfaceType.data[0] low byte selects camera data index; CameraData stores setting/count/position-vector offset; position vectors are grouped as BGCAM_POS, BGCAM_ROT, BGCAM_FOV/JFIF/extra";
constexpr const char* kNativeCameraNormal0Basis =
    "CAM_SET_NORMAL0/CAM_MODE_NORMAL uses CAM_FUNCDATA_NORM1(-20,200,300,10,12,10,35,60,60,0x0003); Camera_InitPlayerSettings starts eyeNextAtOffset.r=180 and pitch=0x071C, then Camera_Normal1 follows player focus using Player_GetHeight, R_CAM_YOFFSET_NORM=OREG(46), at lerp CAM_DATA_AT_LERP_STEP_SCALE/100, and Camera_ClampDist";
constexpr const char* kNativeCameraNormal1YawBasis =
    "Camera_Normal1 updates yaw with Camera_CalcDefaultYaw: yaw delta toward BINANG_ROT180(playerYaw), NEXTPCT(CAM_DATA_MAX_YAW_UPDATE), speedRatio from OREG(8), OREG(50)+OREG(51) startSwingTimer, and update-rate registers from sOREGInit";
constexpr const char* kNativeCameraInputDirectionBasis =
    "Camera_Normal1 exports player input through Camera_GetInputDirYaw from OLib_Vec3fDiffToVecSphGeo(eye, at); keyboard movement maps I/K/J/L onto that eye->at yaw basis";
constexpr const char* kNativeCameraYawConventionBasis =
    "OOT camera spherical yaw is the at->eye offset; the viewer yaw stores the eye->at view basis, so normal camera follow converts atEyeYaw to -viewYaw instead of treating the two yaw spaces as identical";

struct Args {
    std::string ApplicationName = "OOT3D Native Fast3D Demo";
    std::string ApplicationId = "oot3d_native_fast3d_demo";
    std::string ConfigurationPath = "oot3d_native_fast3d_demo.json";
    std::filesystem::path ManifestPath;
    std::filesystem::path ResourceRoot;
    std::vector<std::filesystem::path> ResourceArchives;
    std::filesystem::path OutputPath;
    std::filesystem::path ScreenshotPath;
    bool ScreenshotSequence = false;
    uint32_t ScreenshotStartFrame = 0;
    uint32_t ScreenshotSequenceInterval = 1;
    bool SelfTest = false;
    uint32_t FrameLimit = 0;
    uint32_t BenchmarkWarmupFrames = 0;
    // Runs a bounded, full-workload throughput measurement. The native-game
    // bootstrap supplies one complete simulation step per host frame while
    // the host disables VSync and its SDL software frame limiter.
    bool ThroughputBenchmark = false;
    // Runs guest execution on its own thread so it overlaps rendering.
    bool GuestThread = false;
    double MaxSeconds = 0.0;
    double FixedDeltaSeconds = 0.0;
    std::filesystem::path InputTimelinePath;
    double MaterialAnimationFrame = 0.0;
    uint32_t Width = 1280;
    uint32_t Height = 720;
    uint32_t AudioSampleRate = 44100;
    uint32_t AudioSampleLength = 1024;
    int32_t AudioDesiredBuffered = 2480;
    std::string AudioClockSource = "host_default";
    int32_t BackendId = -1;
    std::string Renderer = "nri";
    std::string RenderMode = "native_texture";
    int32_t EntranceIndex = -1;
    int32_t NativeSceneSetupOverride = -1;
    std::string NativeSceneSetupSource;
    std::optional<ThreeDsRecomp::Oot3d::Oot3dNativeDemoPlayerClipSelection> NativePlayerClips;
    std::shared_ptr<Oot3dDemoHostPlayerController> PlayerController;
    std::string PlayerControllerId;
    std::string PlayerControllerStatus;
    std::shared_ptr<Oot3dDemoHostActorRuntime> ActorRuntime;
    std::string ActorRuntimeId;
    std::string ActorRuntimeStatus;
    bool IntroCutscenePlayback = false;
    uint16_t IntroCutsceneSourceIndex = kDefaultIntroCutsceneSourceIndex;
    bool TitleIntroPlayback = false;
    uint16_t TitleIntroQdbIndex = 0;
    bool TitleIntroQdbIndexOverride = false;
    double TitleIntroFrame = 0.0;
    bool TitleIntroFrameOverride = false;
    bool TitleIntroActorDiagnostics = false;
    bool TitleIntroTrace = false;
    uint32_t TitleIntroTraceStartFrame = 1;
    uint32_t TitleIntroTraceEndFrame = 360;
    std::filesystem::path TitleIntroRomfsRoot;
    bool CutscenePlayerRequest = false;
    uint16_t CutscenePlayerSceneId = 0;
    uint16_t CutscenePlayerSetupIndex = 0;
    uint16_t CutscenePlayerSourceIndex = 0;
    uint16_t CutscenePlayerOrchestrationIndex = UINT16_MAX;
    uint16_t CutscenePlayerAdapterKind = 0;
    bool CutscenePlayerFrameOverride = false;
    double CutscenePlayerFrame = 0.0;
};

struct Vec3 {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

struct Camera {
    Vec3 Position;
    Vec3 Target;
    Vec3 Up{ 0.0, 1.0, 0.0 };
    double Yaw = 0.0;
    double Pitch = 0.0;
    double MoveSpeed = 120.0;
    double FovDegrees = kNativeCameraNormal0FovDeg;
    bool NativeCameraActive = true;
    std::string NativeBehavior = "normal_follow";
    int NativeSet = kNativeCameraSetNormal0;
    int NativeMode = kNativeCameraModeNormal;
    int NativeFunction = kNativeCameraFuncNormal1;
    int NativeSceneCameraDataIndex = -1;
    int NativeFloorCameraDataIndex = -1;
    int NativeStartCameraDataIndex = -1;
    int NativeSceneCameraSetting = kNativeCameraSetNone;
    int NativeSceneCameraPositionIndex = -1;
    bool NativeSceneCameraApplied = false;
    bool NativeStartSceneCameraApplied = false;
    int NativeSuppressedCameraDataIndex = -1;
    int NativeSuppressedCameraSetting = kNativeCameraSetNone;
    bool NativeUnique0Initialized = false;
    double NativeUnique0Timer = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 NativeUnique0InitialActor{};
    bool NativePrerendPivotYawInitialized = false;
    double NativePrerendPivotYaw = 0.0;
    double NativeDistance = kNativeCameraNormal0EyeDistMax;
    Vec3 NativePositionOffset;
    double NativeYawUpdateRateInv = kNativeCameraNormal0YawUpdateRateTarget;
    double NativePitchUpdateRateInv = kNativeCameraOreg7DefaultPitchUpdateRate;
    double NativeRUpdateRateInv = kNativeCameraOreg6RUpdateRateInv;
    double NativeXzOffsetUpdateRate = kNativeCameraOreg2XzOffsetUpdatePercent / 100.0;
    double NativeYOffsetUpdateRate = kNativeCameraOreg3YOffsetUpdatePercent / 100.0;
    double NativeFovUpdateRate = kNativeCameraOreg4FovUpdatePercent / 100.0;
    double NativeSpeedRatio = 0.0;
    double NativePreviousXzSpeed = 0.0;
    double NativeInputYaw = 0.0;
    double NativeInputPitch = 0.0;
    double NativeNormal1YawVelocity = 0.0;
    double NativeNormal1YawCurve = 0.0;
    double NativeNormal1YawCurveInput = 0.0;
    double NativeNormal1YawVelocityFactor = 0.0;
    double NativeNormal1YawDelta = 0.0;
    double NativeNormal1Accel = 0.0;
    double NativeNormal1StartSwingTimer = 0.0;
    double NativeNormal1SwingYawTarget = 0.0;
    double NativeNormal1DistanceTarget = kNativeCameraNormal0EyeDistMax;
    double NativeNormal1RUpdateRateTarget = kNativeCameraOreg6RUpdateRateInv;
    double NativeAtLerpStepScale = kNativeCameraNormal0AtLerpStepScale;
    double NativeYawUpdateRateTarget = kNativeCameraNormal0YawUpdateRateTarget;
    bool NativeCollisionSupported = false;
    bool NativeCollisionPolygonFlagsSupported = false;
    bool NativeCollisionActive = false;
    int NativeCollisionPolygonIndex = -1;
    int NativeCollisionPreviousPolygonIndex = -1;
    Vec3 NativePreCollisionPosition;
    Vec3 NativeCollisionHitPosition;
    Vec3 NativeCollisionNormal;
    double NativeCollisionRayExtend = kNativeCameraCollisionRayExtend;
    double NativeCollisionSurfacePush = kNativeCameraCollisionSurfacePush;
    double NativeCollisionDisplacement = 0.0;
    int NativeCollisionIgnoredCameraPolygonCount = 0;
    bool NativeCollisionRetainedPreviousPolygon = false;
};
