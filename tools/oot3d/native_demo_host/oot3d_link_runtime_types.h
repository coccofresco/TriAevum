#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "oot3d_demo_math.h"
#include "oot3d_title_intro_runtime_types.h"

constexpr double kOot3dNativePlayerTickRate = 30.0;
constexpr double kOot3dS16AngleToRadians = (kOot3dDemoPi * 2.0) / 65536.0;
constexpr const char* kOot3dNativeCharacterAnimationFrameRateBasis =
    "oot3d_csab_anod_hermite_interval_scale_1_over_30";

enum class Oot3dNativeBoots : size_t {
    Kokiri,
    Iron,
    Hover,
    Unused3,
    IronUnderwater,
    KokiriChild,
};

struct Oot3dNativeBootData {
    int16_t Reg19;
    int16_t Reg30;
    int16_t Reg32;
    int16_t Reg34;
    int16_t Reg35;
    int16_t Reg36;
    int16_t Reg37;
    int16_t Reg38;
    int16_t Reg43;
    int16_t Reg45;
    int16_t Reg68;
    int16_t Reg69;
    int16_t Ireg66;
    int16_t Ireg67;
    int16_t Ireg68;
    int16_t Ireg69;
    int16_t Mreg95;
};

constexpr Oot3dNativeBootData kOot3dNativeBootData[] = {
    { 200, 1000, 300, 700, 550, 270, 600, 350, 800, 600, -100, 600, 590, 750, 125, 200, 130 },
    { 200, 1000, 300, 700, 550, 270, 1000, 0, 800, 300, -160, 600, 590, 750, 125, 200, 130 },
    { 200, 1000, 300, 700, 550, 270, 600, 600, 800, 550, -100, 600, 540, 270, 25, 0, 130 },
    { 200, 1000, 300, 700, 380, 400, 0, 300, 800, 500, -100, 600, 590, 750, 125, 200, 130 },
    { 80, 800, 150, 700, 480, 270, 600, 50, 800, 550, -40, 400, 540, 270, 25, 0, 80 },
    { 200, 1000, 300, 800, 500, 400, 800, 400, 800, 550, -100, 600, 540, 750, 125, 400, 200 },
};

constexpr Oot3dNativeBoots kDemoNativeBoots = Oot3dNativeBoots::KokiriChild;
constexpr Oot3dNativeBootData kDemoNativeBootData =
    kOot3dNativeBootData[static_cast<size_t>(kDemoNativeBoots)];
constexpr double NativeRegHundredthsToUnitsPerTick(int16_t value) {
    return static_cast<double>(value) / 100.0;
}
constexpr double kNativeRunSpeedUnitsPerTick = NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Reg45);
constexpr double kNativeRunSpeedUnitsPerSecond = kNativeRunSpeedUnitsPerTick * kOot3dNativePlayerTickRate;
constexpr double kNativeLinearAccelUnitsPerTick = NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Reg19);
constexpr double kNativeLinearDecelUnitsPerTick = 1.5;
constexpr double kNativeDecelerateToZeroUnitsPerTick = NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Reg43);
constexpr double kNativeLargeYawBrakeThresholdS16 = 0x6000;
constexpr double kNativeLargeYawBrakeThresholdRadians = kNativeLargeYawBrakeThresholdS16 * kOot3dS16AngleToRadians;
constexpr double kPlayerSetBootDataDefaultYawStepS16PerTick = 2000.0;
constexpr double kPlayerUpdateShapeYawStepS16PerTick = 2000.0;
constexpr const char* kMovingLinkYawBasis =
    "Idle speedTarget!=0 uses func_8083C8DC shape=yaw=yawTarget; running uses func_8083C484 large-yaw brake before func_8083DF68 Math_ScaledStepToS(&this->yaw, yawTarget, REG(27)); Player_SetBootData REG(27)=2000";
constexpr const char* kLinkMovementSpeedBasis =
    "sBootData[PLAYER_BOOTS_KOKIRI_CHILD][9]=550; R_RUN_SPEED_LIMIT/100=5.5 units/tick at 30Hz";
constexpr const char* kLinkLinearAccelerationBasis =
    "func_8083DF68 Math_AsymStepToF(&linearVelocity, speedTarget, REG(19)/100, 1.5); sBootData child Kokiri REG(19)=200";
constexpr const char* kLinkLargeYawBrakeBasis =
    "func_8083C484 if ABS(this->yaw - yawTarget) > 0x6000 then Player_DecelerateToZero using REG(43)/100";
constexpr int16_t kPlayerSetBootDataDefaultWalkRunThresholdReg48 = 370;
constexpr double kNativeWalkRunThresholdUnitsPerTick =
    NativeRegHundredthsToUnitsPerTick(kPlayerSetBootDataDefaultWalkRunThresholdReg48);
constexpr double kNativeWalkAnimationBaseScale = static_cast<double>(kDemoNativeBootData.Reg35) / 1000.0;
constexpr double kNativeWalkAnimationVelocityScalePerUnit = static_cast<double>(kDemoNativeBootData.Reg36) / 1000.0;
constexpr double kNativeWalkRunBlendScalePerUnit = static_cast<double>(kDemoNativeBootData.Reg37) / 1000.0;
constexpr double kNativeRunAnimationBaseScale = 1.2;
constexpr double kNativeRunAnimationVelocityScalePerUnit = static_cast<double>(kDemoNativeBootData.Reg38) / 1000.0;
constexpr double kNativeLocomotionCycleFrameSpan = 29.0;
constexpr double kNativeRunFrameScaleFromLocomotionCycle = 20.0 / 29.0;
constexpr const char* kLinkLocomotionAnimationBasis =
    "func_80841EE4 selects PLAYER_ANIMGROUP_walk while linearVelocity < REG(48)/100; Player_SetBootData REG(48)=370; walk speed scale uses REG(35)/1000 + REG(36)/1000*linearVelocity";
constexpr const char* kLinkLocomotionBlendBasis =
    "func_80841EE4 advances shared unk_868 modulo 29, blends walk/run with temp1=REG(37)/1000*(linearVelocity-REG(48)/100), samples run at unk_868*(20/29), and uses LinkAnimation_InterpJointMorph for partial blends";
constexpr const char* kLinkLocomotionPoseBlendSpace =
    "current demo blend samples native OOT3D CSAB poses then linearly blends bone world transforms before skinning";
constexpr double kNativeWalkEndPhaseOffset = 3.0;
constexpr double kNativeWalkEndLeftPhaseThreshold = 14.0;
constexpr double kNativeWalkEndLeftPhaseCenter = 11.0;
constexpr double kNativeWalkEndRightPhaseCenter = 26.0;
constexpr double kNativeWalkEndLeftMorphDenominator = 11.0;
constexpr double kNativeWalkEndRightMorphDenominator = 12.0;
constexpr double kNativeWalkEndLeftLatePhaseScale = 1.375;
constexpr double kNativeWalkEndRightLatePhaseScale = 2.0;
constexpr double kNativeWalkEndMorphFrameScale = 4.0;
constexpr double kNativeDefaultLoopMorphFrames = 6.0;
constexpr const char* kLinkWalkEndBasis =
    "func_8083BF50 computes sp30=unk_868-3 wrapped to 0..29, chooses PLAYER_ANIMGROUP_walk_endL when sp30<14 else walk_endR, and passes morph frames 4.0*phaseWeight to LinkAnimation_Change";
constexpr const char* kLinkIdleMorphBasis =
    "Player_AnimChangeLoopMorph uses LinkAnimation_Change(..., ANIMMODE_LOOP, -6.0f); demo applies the same 6-frame pose morph when walk_end returns to idle";
constexpr double kNativeFloorRaycastHeightAbovePreviousY = 50.0;
constexpr double kNativeGroundSnapDropLimit = 11.0;
constexpr const char* kNativeFloorProbeBasis =
    "Actor_UpdateBgCheckInfo calls BgCheck_EntityRaycastFloor5 from previous actor Y + 50, accepts the nearest floor strictly below that ray point, and only keeps ground continuity for small drops";
constexpr double kNativeMinimumVerticalVelocityUnitsPerTick = -20.0;
constexpr double kSemanticShortLandingMaximumFallDistance = 80.0;
constexpr double kSemanticAutoJumpMinimumFloorDistance = 20.0;
constexpr double kSemanticAutoJumpMaximumYawDifferenceS16 = 0x2000;
constexpr double kSemanticRunAutoJumpMaximumYawDifferenceS16 = 0x1000;
constexpr double kSemanticAutoJumpMinimumSpeedUnitsPerTick = 3.0;
constexpr double kSemanticRunAutoJumpMinimumSpeedUnitsPerTick = 4.0;
constexpr const char* kNativeVerticalMotionBasis =
    "OOT3D code.bin Actor_MoveForward@00376864 integrates actor velocity.y with gravity and clamps it to actor minVelocityY; Player update@00250AD0 writes minVelocityY=-20.0 from code.bin@00252934 and gravity=REG(68)*0.01";
constexpr const char* kShortLandingSemanticBasis =
    "N64 gameplay scaffold Player_Action_8084411C selects PLAYER_ANIMGROUP_short_landing for fallDistance<=80; OOT3D group rows 17/18 and native CSAB members independently prove the short/normal landing asset split";
constexpr const char* kNativeAutoJumpBasis =
    "N64 gameplay scaffold selects auto-jump after a >20-unit floor loss with aligned movement and speed >3; OOT3D code.bin boot IREG(66..69) supplies the jump velocity function and native ZAR uniquely supplies nml_jump/nml_run_jump/nml_run_jump_end";
constexpr int kNativeWallNormalYThreshold = 12000;
constexpr double kNativePlayerWallCheckHeight = 26.0;
constexpr double kNativeWallSpeedScalePerS16 = 0.00008;
constexpr double kNativeWallMinimumSpeedLimitUnitsPerTick = 0.1;
constexpr int kNativeWallCollisionPasses = 2;
constexpr const char* kNativeHorizontalCollisionBasis =
    "Player_ProcessSceneCollision/Actor_UpdateBgCheckInfo style XZ sphere-vs-wall pass against OOT3D ZSI collision polygon segments, using ageProperties wallCheckRadius and wallCheckHeight=26";
constexpr const char* kNativeWallSpeedLimitBasis =
    "Player_ProcessSceneCollision computes unk_880 from worldYawToTouchedWall*0.00008, clamps grounded wall speed to max(R_RUN_SPEED_LIMIT*scale, 0.1), and movement target speed clamps to unk_880";
constexpr const char* kNativeCameraBgCamBasis =
    "z_play applies ACTOR_PLAYER.params&0xff with Camera_ChangeDataIdx at spawn; Camera_GetDataIdxForPoly later changes only to non-CAM_SET_NONE SurfaceType_GetCamDataIndex records; PREREND_FIXED runs CAM_FUNC_FIXD3 and PREREND_PIVOT runs CAM_FUNC_UNIQ7";
constexpr const char* kNativeCameraCollisionBasis =
    "Camera_BGCheckInfo extends the at-to-eye ray by 8 units, calls BgCheck_CameraLineTest1(COLPOLY_IGNORE_CAMERA, chkWall/floor/ceil, chkOneFace), and moves eye to intersection plus collision poly normal";
constexpr const char* kNativeCameraCollisionPolygonFlagBasis =
    "z64bgcheck COLPOLY_VIA_FLAG_TEST(flags_vIA, flags) maps COLPOLY_IGNORE_CAMERA(1<<0) to flags_vIA bit 0x2000; BgCheck_CameraLineTest1 skips those polygons";
constexpr const char* kNativeCameraCollisionStabilityBasis =
    "Camera collision keeps state across frames; the demo reuses the previous collision polygon only when the current COLPOLY_IGNORE_CAMERA line test still finds it at the nearest hit distance";
constexpr const char* kNativeCameraUnique0Basis =
    "CAM_SET_START1 runs Camera_Unique0: eye comes from BGCAM_POS, at is the closest point on the BGCAM_ROT ray toward the player, and the camera returns to the previous setting once the player moves after the BGCAM_JFIFID timer";

enum class LinkNativeMovementAction {
    Idle,
    Run,
};

enum class LinkNativeLocomotionState {
    Idle,
    StartMove,
    Locomotion,
    WallLimited,
    WalkEnd,
    ReturnIdle,
};

enum class LinkNativePlayerAction {
    Locomotion,
    SceneEntrance,
    AutoJump,
    Airborne,
    Landing,
    LedgeHold,
    LedgeClimb,
    SurfaceClimb,
};

enum class LinkNativeAutoJumpKind {
    Normal,
    Run,
};

enum class LinkNativeClimbMode : int8_t {
    None = -1,
    RegularLadder = 0,
    FreeSurface = 2,
};

enum class LinkNativeLandingKind {
    Normal,
    Short,
    RunAutoJump,
};

struct LinkInstance {
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 Position;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 PositionToActorOffset;
    double Yaw = 0.0;
    double MovementYaw = 0.0;
    double Scale = 1.0;
    double MoveSpeed = kNativeRunSpeedUnitsPerSecond;
    double NativeLinearVelocityUnitsPerTick = 0.0;
    double NativeSpeedTargetUnitsPerTick = kNativeRunSpeedUnitsPerTick;
    double NativeLinearAccelUnitsPerTick = kNativeLinearAccelUnitsPerTick;
    double NativeLinearDecelUnitsPerTick = kNativeLinearDecelUnitsPerTick;
    double NativeDecelerateToZeroUnitsPerTick = kNativeDecelerateToZeroUnitsPerTick;
    double MovingYawStepS16PerTick = kPlayerSetBootDataDefaultYawStepS16PerTick;
    double ShapeYawStepS16PerTick = kPlayerUpdateShapeYawStepS16PerTick;
    LinkNativeMovementAction NativeAction = LinkNativeMovementAction::Idle;
    double ColliderRadius = 12.0;
    double ColliderHeight = 56.0;
    bool NativeCollisionGrounding = false;
    bool Grounded = false;
    double GroundY = 0.0;
    double NativeFloorHeightDiff = 0.0;
    int FloorPolygonIndex = -1;
    int FloorSurfaceType = -1;
    LinkNativePlayerAction NativePlayerAction = LinkNativePlayerAction::Locomotion;
    int NativeSceneEntranceStartMode = -1;
    int NativeSceneEntranceTimer = 0;
    double NativeSceneEntranceTickAccumulator = 0.0;
    double NativeSceneEntranceSpeedUnitsPerTick = 0.0;
    double NativeSceneEntranceDistance = 0.0;
    bool NativeSceneEntranceTargetCaptured = false;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 NativeSceneEntranceTargetActorPosition;
    LinkNativeAutoJumpKind NativeAutoJumpKind = LinkNativeAutoJumpKind::Normal;
    LinkNativeLandingKind NativeLandingKind = LinkNativeLandingKind::Short;
    double NativeVerticalVelocityUnitsPerTick = 0.0;
    double NativeGravityUnitsPerTickSquared =
        NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Reg68);
    double NativeMinimumVerticalVelocityUnitsPerTick = kNativeMinimumVerticalVelocityUnitsPerTick;
    double NativePhysicsTickAccumulator = 0.0;
    double NativeFallStartY = 0.0;
    double NativeFallDistance = 0.0;
    bool NativeHorizontalCollision = false;
    bool HorizontalCollision = false;
    int WallPolygonIndex = -1;
    int HorizontalCollisionIterationCount = 0;
    double WallPushNormalX = 0.0;
    double WallPushNormalZ = 0.0;
    double NativeWallSpeedLimitUnitsPerTick = kNativeRunSpeedUnitsPerTick;
    double NativeWallSpeedScale = 1.0;
    double NativeWorldYawToWallS16 = 0.0;
    bool NativeWallSpeedLimitActive = false;
    double NativeWallCheckHeight = kNativePlayerWallCheckHeight;
    double NativeWallSpeedScalePerS16 = kNativeWallSpeedScalePerS16;
    double NativeMinimumWallSpeedLimitUnitsPerTick = kNativeWallMinimumSpeedLimitUnitsPerTick;
    int NativeLedgeWallPolygonIndex = -1;
    int NativeLedgeFloorPolygonIndex = -1;
    int NativeLedgeClimbType = 0;
    double NativeLedgeYDistance = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 NativeLedgeHangActorPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 NativeLedgeTopActorPosition;
    double NativeLedgePushNormalX = 0.0;
    double NativeLedgePushNormalZ = 0.0;
    std::string NativeLedgeQueryStatus = "unavailable";
    int NativeSurfaceClimbWallPolygonIndex = -1;
    LinkNativeClimbMode NativeClimbMode = LinkNativeClimbMode::None;
    uint32_t NativeSurfaceClimbWallFlags = 0;
    double NativeSurfaceClimbMinimumY = 0.0;
    double NativeSurfaceClimbMaximumY = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 NativeSurfaceClimbAnchorActorPosition;
    double NativeSurfaceClimbPushNormalX = 0.0;
    double NativeSurfaceClimbPushNormalZ = 0.0;
    int NativeSurfaceClimbTopFloorPolygonIndex = -1;
    double NativeSurfaceClimbTopFloorY = 0.0;
    std::string NativeSurfaceClimbQueryStatus = "unavailable";
};

struct LinkMotionState {
    bool HasInput = false;
    bool Moving = false;
    Vec3 Direction;
    Vec3 MovementDirection;
    std::string NativeAction;
    std::string YawAlignmentMode;
    bool LargeYawBrake = false;
    double Speed = 0.0;
    double SpeedRatio = 0.0;
    double NativeSpeedTargetUnitsPerTick = 0.0;
    double NativeLinearVelocityUnitsPerTick = 0.0;
    double NativeVelocityStepUnitsPerTick = 0.0;
    double NativeWallSpeedLimitUnitsPerTick = kNativeRunSpeedUnitsPerTick;
    bool NativeWallSpeedLimitActive = false;
    bool NativeWallLimitedWalk = false;
    std::string NativePlayerAction = "locomotion";
    int NativeSceneEntranceStartMode = -1;
    int NativeSceneEntranceTimer = 0;
    double NativeSceneEntranceSpeedUnitsPerTick = 0.0;
    double NativeSceneEntranceDistance = 0.0;
    bool NativeSceneEntranceTargetCaptured = false;
    std::string NativeAutoJumpKind;
    std::string NativeLandingKind;
    double NativeVerticalVelocityUnitsPerTick = 0.0;
    double NativeFallDistance = 0.0;
    int NativeLedgeClimbType = 0;
    double NativeLedgeYDistance = 0.0;
    std::string NativeLedgeQueryStatus = "unavailable";
    std::string NativeClimbMode = "none";
    uint32_t NativeSurfaceClimbWallFlags = 0;
    double NativeSurfaceClimbMinimumY = 0.0;
    double NativeSurfaceClimbMaximumY = 0.0;
    std::string NativeSurfaceClimbQueryStatus = "unavailable";
    double NativeWalkRunThresholdUnitsPerTick = kNativeWalkRunThresholdUnitsPerTick;
    std::string LocomotionAnimationClass = "idle";
    double TargetYaw = 0.0;
    double YawDelta = 0.0;
    double YawStepRadians = 0.0;
    double MovementYaw = 0.0;
    double ShapeYaw = 0.0;
};

struct LinkNativeClipRegistry {
    size_t IdleClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
    size_t WalkClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
    size_t RunClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
    size_t WalkEndLeftClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
    size_t WalkEndRightClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
};

struct LinkNativeCollisionActionConfig {
    double WallCheckRadius = 0.0;
    double StandingWallCheckHeight = 0.0;
    double LedgeWallProbeHeight = 0.0;
    double WallProbeForwardAddend = 0.0;
    double LedgeFloorProbeHeight = 0.0;
    double MinimumLedgeY = 0.0;
    double InvalidLedgeY = 0.0;
    double CeilingClearanceAboveLedge = 0.0;
    double UpperWallProbeAboveLedge = 0.0;
    double LedgeType2MinimumY = 0.0;
    double LedgeType3MinimumY = 0.0;
    double LedgeType4MinimumY = 0.0;
    double WallYawSpeedScale = 0.0;
    double MinimumWallSpeed = 0.0;
    int ShapeYawToWallMaximumS16 = 0;
    int UpperWallYawDifferenceMaximumS16 = 0;
    int WallNormalYAbsMaximumExclusive = 0;
    int FloorNormalYAbsMinimumExclusive = 0;
    uint32_t CurrentWallRejectFlagMask = 0;
    uint32_t UpperWallClearanceFlagMask = 0;
    int MinimumClimbTypeForFallingGrab = 0;
    bool FallingGrabRequiresDescending = true;
    bool FallingGrabRequiresForwardSpeed = true;
    double FallingGrabMaximumFallDistance = 0.0;
    double FallingGrabMinimumLedgeAboveFloor = 0.0;
    double HangWallPlaneOffset = 0.0;
    double ClimbInputMinimumWallwardDotExclusive = 0.0;
    uint32_t FreeClimbWallFlagMask = 0;
    uint32_t RegularLadderWallFlagMask = 0;
    int RegularLadderModeValue = 0;
    int FreeClimbModeValue = 0;
    int GroundClimbInitialPhase = 0;
    double GroundClimbMinimumYDistanceToLedge = 0.0;
    double GroundClimbLateralAlignmentMaximumExclusive = 0.0;
    double GroundClimbRungInterval = 0.0;
    double GroundClimbWallPlaneInset = 0.0;
    double SurfaceClimbHorizontalInputPlaySpeedScale = 0.0;
    double SurfaceClimbVerticalInputPlaySpeedScale = 0.0;
    double SurfaceClimbMinimumPlaySpeed = 0.0;
    double SurfaceClimbMaximumPlaySpeed = 0.0;
    double SurfaceClimbBottomDismountFloorDelta = 0.0;
    double SurfaceClimbTopReachHeight = 0.0;
    double SurfaceClimbTopFloorProbeForwardDistance = 0.0;
    double SurfaceClimbDismountPlaySpeed = 0.0;
    uint32_t SurfaceBehaviorIndexShift = 0;
    uint32_t SurfaceBehaviorIndexMask = 0;
    std::array<uint32_t, 32> SurfaceWallFlags{};
};

struct LinkNativePlayerActionConfig {
    uint32_t StartModeMask = 0;
    uint32_t StartModeShift = 0;
    int SceneEntranceIdleStartMode = -1;
    int SceneEntranceSlowStartMode = -1;
    int SceneEntranceForwardStartMode = -1;
    double SceneEntranceIdleTargetDistance = 0.0;
    int SceneEntranceIdleTimer = 0;
    double SceneEntranceSlowSpeedUnitsPerTick = 0.0;
    double SceneEntranceSlowTargetDistance = 0.0;
    int SceneEntranceSlowTimer = 0;
    double SceneEntranceForwardMinimumSpeedUnitsPerTick = 0.0;
    double SceneEntranceForwardTargetDistance = 0.0;
    double SceneEntranceForwardTimerNumerator = 0.0;
    int SceneEntranceForwardMinimumTimer = 0;
    double SceneEntranceInitialLinearSpeedUnitsPerTick = 0.0;
    double SceneEntranceDefaultTargetSpeedUnitsPerTick = 0.0;
    double SceneEntranceFloorProbeYOffset = 0.0;
    int SceneEntranceTargetCaptureDistance = 0;
    int SceneEntranceCompletionDistance = 0;
};

struct LinkNativeLocomotionConfig {
    Oot3dNativeBoots Boots = kDemoNativeBoots;
    Oot3dNativeBootData BootData = kDemoNativeBootData;
    int16_t WalkRunThresholdReg48 = kPlayerSetBootDataDefaultWalkRunThresholdReg48;
    double CharacterFramesPerSecond = kOot3dNativeCharacterAnimationFramesPerSecond;
    double PlayerTickRate = kOot3dNativePlayerTickRate;
    double RunSpeedUnitsPerTick = kNativeRunSpeedUnitsPerTick;
    double RunSpeedUnitsPerSecond = kNativeRunSpeedUnitsPerSecond;
    double LinearAccelUnitsPerTick = kNativeLinearAccelUnitsPerTick;
    double LinearDecelUnitsPerTick = kNativeLinearDecelUnitsPerTick;
    double DecelerateToZeroUnitsPerTick = kNativeDecelerateToZeroUnitsPerTick;
    double LargeYawBrakeThresholdS16 = kNativeLargeYawBrakeThresholdS16;
    double LargeYawBrakeThresholdRadians = kNativeLargeYawBrakeThresholdRadians;
    double MovingYawStepS16PerTick = kPlayerSetBootDataDefaultYawStepS16PerTick;
    double ShapeYawStepS16PerTick = kPlayerUpdateShapeYawStepS16PerTick;
    double WalkRunThresholdUnitsPerTick = kNativeWalkRunThresholdUnitsPerTick;
    double WalkAnimationBaseScale = kNativeWalkAnimationBaseScale;
    double WalkAnimationVelocityScalePerUnit = kNativeWalkAnimationVelocityScalePerUnit;
    double WalkRunBlendScalePerUnit = kNativeWalkRunBlendScalePerUnit;
    double RunAnimationBaseScale = kNativeRunAnimationBaseScale;
    double RunAnimationVelocityScalePerUnit = kNativeRunAnimationVelocityScalePerUnit;
    double LocomotionCycleFrameSpan = kNativeLocomotionCycleFrameSpan;
    double RunFrameScaleFromLocomotionCycle = kNativeRunFrameScaleFromLocomotionCycle;
    double WalkEndPhaseOffset = kNativeWalkEndPhaseOffset;
    double WalkEndLeftPhaseThreshold = kNativeWalkEndLeftPhaseThreshold;
    double WalkEndLeftPhaseCenter = kNativeWalkEndLeftPhaseCenter;
    double WalkEndRightPhaseCenter = kNativeWalkEndRightPhaseCenter;
    double WalkEndLeftMorphDenominator = kNativeWalkEndLeftMorphDenominator;
    double WalkEndRightMorphDenominator = kNativeWalkEndRightMorphDenominator;
    double WalkEndLeftLatePhaseScale = kNativeWalkEndLeftLatePhaseScale;
    double WalkEndRightLatePhaseScale = kNativeWalkEndRightLatePhaseScale;
    double WalkEndMorphFrameScale = kNativeWalkEndMorphFrameScale;
    double DefaultLoopMorphFrames = kNativeDefaultLoopMorphFrames;
    double GravityUnitsPerTickSquared =
        NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Reg68);
    double MinimumVerticalVelocityUnitsPerTick = kNativeMinimumVerticalVelocityUnitsPerTick;
    double ShortLandingMaximumFallDistance = kSemanticShortLandingMaximumFallDistance;
    double AutoJumpMinimumFloorDistance = kSemanticAutoJumpMinimumFloorDistance;
    double AutoJumpMaximumYawDifferenceS16 = kSemanticAutoJumpMaximumYawDifferenceS16;
    double RunAutoJumpMaximumYawDifferenceS16 = kSemanticRunAutoJumpMaximumYawDifferenceS16;
    double AutoJumpMinimumSpeedUnitsPerTick = kSemanticAutoJumpMinimumSpeedUnitsPerTick;
    double RunAutoJumpMinimumSpeedUnitsPerTick = kSemanticRunAutoJumpMinimumSpeedUnitsPerTick;
    double AutoJumpHighSpeedThresholdUnitsPerTick =
        NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Ireg66);
    double AutoJumpHighVerticalVelocityUnitsPerTick =
        NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Ireg67);
    double AutoJumpBaseVerticalVelocityUnitsPerTick =
        NativeRegHundredthsToUnitsPerTick(kDemoNativeBootData.Ireg68);
    double AutoJumpSpeedVelocityScalePerUnit =
        static_cast<double>(kDemoNativeBootData.Ireg69) / 1000.0;
    LinkNativeClipRegistry Clips;
};

struct LinkAnimation {
    size_t ClipIndex = 0;
    std::string ClipId;
    std::string CsabName;
    double Frame = 0.0;
    double NativeCycleFrame = 0.0;
    double WalkFrame = 0.0;
    double RunFrame = 0.0;
    double FramesPerSecond = kOot3dNativeCharacterAnimationFramesPerSecond;
    double NativeCycleFramesPerSecond = 0.0;
    uint32_t FrameCount = 0;
    bool Moving = false;
    bool NativeWallLimitedWalk = false;
    double NativeWalkRunThresholdUnitsPerTick = kNativeWalkRunThresholdUnitsPerTick;
    bool NativeLocomotionControllerActive = true;
    std::string NativeLocomotionState = "idle";
    std::string LocomotionAnimationClass = "idle";
    std::string PreviousLocomotionAnimationClass = "idle";
    double NativeWalkRunBlendWeight = 0.0;
    bool NativeWalkRunBlendActive = false;
    double NativeStartBlendWeight = 1.0;
    bool NativeStartMorphActive = false;
    bool NativeWalkEndActive = false;
    bool NativeWalkEndEntryMorphActive = false;
    bool NativeWalkEndToIdleMorphActive = false;
    bool NativeWalkEndComplete = false;
    std::string NativeWalkEndSide;
    double NativeWalkEndSelectionPhase = 0.0;
    double NativeWalkEndEntryMorphFrames = 0.0;
    double NativeWalkEndEntryBlendWeight = 1.0;
    double NativeWalkEndToIdleBlendWeight = 1.0;
    bool PoseBlendActive = false;
    std::string PoseBlendFromClipId;
    std::string PoseBlendToClipId;
    double PoseBlendWeight = 1.0;
    ThreeDsRecomp::Oot3d::CsabPose StartMorphPose;
    bool StartMorphPoseValid = false;
    ThreeDsRecomp::Oot3d::CsabPose WalkEndEntryPose;
    bool WalkEndEntryPoseValid = false;
    ThreeDsRecomp::Oot3d::CsabPose WalkEndToIdlePose;
    bool WalkEndToIdlePoseValid = false;
    ThreeDsRecomp::Oot3d::CsabPose CurrentPose;
};

inline const char* NativeMovementActionName(LinkNativeMovementAction action) {
    switch (action) {
        case LinkNativeMovementAction::Idle:
            return "idle";
        case LinkNativeMovementAction::Run:
            return "run";
    }
    return "unknown";
}

inline const char* NativeLocomotionStateName(LinkNativeLocomotionState state) {
    switch (state) {
        case LinkNativeLocomotionState::Idle:
            return "idle";
        case LinkNativeLocomotionState::StartMove:
            return "start_move";
        case LinkNativeLocomotionState::Locomotion:
            return "locomotion";
        case LinkNativeLocomotionState::WallLimited:
            return "wall_limited";
        case LinkNativeLocomotionState::WalkEnd:
            return "walk_end";
        case LinkNativeLocomotionState::ReturnIdle:
            return "return_idle";
    }
    return "unknown";
}

inline const char* NativePlayerActionName(LinkNativePlayerAction action) {
    switch (action) {
        case LinkNativePlayerAction::Locomotion:
            return "locomotion";
        case LinkNativePlayerAction::SceneEntrance:
            return "scene_entrance";
        case LinkNativePlayerAction::AutoJump:
            return "auto_jump";
        case LinkNativePlayerAction::Airborne:
            return "airborne";
        case LinkNativePlayerAction::Landing:
            return "landing";
        case LinkNativePlayerAction::LedgeHold:
            return "ledge_hold";
        case LinkNativePlayerAction::LedgeClimb:
            return "ledge_climb";
        case LinkNativePlayerAction::SurfaceClimb:
            return "surface_climb";
    }
    return "unknown";
}

inline const char* NativeAutoJumpKindName(LinkNativeAutoJumpKind kind) {
    switch (kind) {
        case LinkNativeAutoJumpKind::Normal:
            return "normal";
        case LinkNativeAutoJumpKind::Run:
            return "run";
    }
    return "unknown";
}

inline const char* NativeLandingKindName(LinkNativeLandingKind kind) {
    switch (kind) {
        case LinkNativeLandingKind::Normal:
            return "normal";
        case LinkNativeLandingKind::Short:
            return "short";
        case LinkNativeLandingKind::RunAutoJump:
            return "run_auto_jump";
    }
    return "unknown";
}

inline const char* NativeClimbModeName(LinkNativeClimbMode mode) {
    switch (mode) {
        case LinkNativeClimbMode::None:
            return "none";
        case LinkNativeClimbMode::RegularLadder:
            return "regular_ladder";
        case LinkNativeClimbMode::FreeSurface:
            return "free_surface";
    }
    return "unknown";
}
