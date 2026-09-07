#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeEffectSs.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

extern "C" {
#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_frame_runtime.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_frame_runtime.h"
#include "oot3d/title_intro_opening_logo_runtime.h"
#include "oot3d/title_intro_player_resource_visibility.h"
#include "oot3d/title_intro_source_table.h"
}

constexpr double kOot3dNativeCharacterAnimationFramesPerSecond = 30.0;
constexpr uint32_t kOot3dTitleIntroPlayerActionCommandId = 0x0000000A;
constexpr uint32_t kOot3dTitleIntroEponaCueCommandId = 0x0000003E;
constexpr uint32_t kOot3dTitleIntroLogoMaterialColorSlot = 5;
constexpr uint16_t kOot3dInvalidSourceIndex = 0xFFFF;

constexpr const char* kOot3dTitleIntroRuntimeBasis =
    "decomp_support z_title_intro_opening_orchestration.c and z_title_intro_opening_actor_runtime.c bind the native spot99 setup-1 title cutscene to adult Link/Epona/logo actor sources; demo runtime resolves opening actor assets through those native orchestration rows before sampling QDB/cutscene motion";
constexpr const char* kOot3dTitleIntroInitialSceneCameraBasis =
    "spot99_info.zsi setup 1 native cutscene command 0x97 camera path; selected from the generated scene cutscene timeline and validated against Azahar PICA camera uniform traces";
constexpr const char* kOot3dTitleIntroLogoBindingBasis =
    "N64 En_Mag is used as the title-logo actor structure cross-check; OOT3D code.bin ActorInit and EnMag_Init are the binding source for ACTOR_EN_MAG/OBJECT_MAG and zelda_mag.zar native CMB/CSAB type-local indices";
constexpr const char* kOot3dTitleIntroLogoDrawProjectionBasis =
    "native EnMag_Draw CMB handle/matrix/color and submit-manager small-queue route are decoded from code.bin/Ghidra evidence; standalone render binding applies draw-snapshot matrices and material color slot 5, while full runtime/three_ds_recomp submit-manager parity remains pending";
constexpr const char* kOot3dTitleIntroLogoDrawRenderBindingBasis =
    "native EnMag_Draw draw snapshot applies OOT3D component visibility, row-major matrices, and material constant-color slot 5 RGBA to zelda_mag.zar CMB render components";
constexpr const char* kOot3dTitleIntroLogoUpdateBasis =
    "N64 En_Mag is used to name the title-logo state shape; OOT3D EnMag_Init/EnMag_Update code.bin literals and callsites are the source for alpha fields, fade states, env flag gates, and CMB/CSAB animation updates";
constexpr const char* kOot3dTitleIntroLogoAlphaSimulationBasis =
    "diagnostic EnMag alpha-state interpreter executes the decoded OOT3D EnMag_Init/EnMag_Update rows; N64 En_Mag is used only as the logical state-shape cross-check";
constexpr const char* kOot3dTitleIntroActorAnimationBindingBasis =
    "generated title-intro actor animation rows decode OOT3D actor-init CMB/CSAB table slots and the native title horse cue dispatcher from code.bin and native ZAR contents; N64 horse cutscene state is only a gameplay structure cross-check";
constexpr const char* kOot3dTitleIntroSkelAnimeTimingBasis =
    "generated SkelAnime timing rows decode Animation_Change mode mapping, SkelAnime_Update scale literals, and GameState global update rate initialization directly from OOT3D code.bin; Azahar traces validate resulting frames but are not runtime inputs";
constexpr const char* kOot3dTitleIntroHorseStateRouteBasis =
    "generated title-intro horse state rows decode OOT3D EnHorse Idle/SetFollowAnimation/StartMovingAnimation/UpdateSpeed/MountedWalk/MountedTrot/MountedGallop route evidence from code.bin and focused Ghidra exports; N64 EnHorse is only the gameplay structure cross-check";
constexpr const char* kOot3dTitleIntroLinkChildStateRouteBasis =
    "generated title-intro Link-boy actor-symbol rows retain OOT3D EnHorseLinkChild_Update actor+0x1A4 action-table dispatch and native CSAB table addresses from code.bin because that native route loads actor/zelda_link_opening.zar; this is source-symbol evidence, not the title-intro Link boy player-action timeline identity";
constexpr const char* kOot3dTitleIntroLinkBoyPlayerActionBasis =
    "generated title-intro adult-Link player-action rows decode spot00_demo_epona_* QDB command 0x0A / CS_CMD_SET_PLAYER_ACTION cue entries; N64 z_demo/z_player is only the consumer-architecture reference pending the OOT3D cue-id consumer split";
constexpr const char* kOot3dTitleIntroActorScaleBasis =
    "opening actor CMB-to-scene conversion uses direct OOT3D Actor_SetScale VFP s0 literals decoded from EnHorseLinkChild_Init/EnHorse_Init code.bin callsites; Oot3dNativeDemoScene.LinkScale is only a fallback for unresolved actor scale rows, and N64 horse actor sources are only the semantic init-structure cross-check";
constexpr const char* kOot3dTitleIntroMountedLinkPoseFrameBasis =
    "OOT3D mounted-player action 0x002b7fd0 keeps a ride actor pointer and writes Player SkelAnime currentFrame from 0x004c5510; the generated code.bin route returns horseFrame directly by default and applies VFP int(horseFrame * 2/3 + 0.5) for horse animation indices 4..9, so mounted Link follows the persistent ride-actor animation state instead of restarting from each QDB cue";

struct TitleIntroCueSample {
    bool Valid = false;
    bool Active = false;
    const Oot3dTitleIntroActorCueRow* Row = nullptr;
    const Oot3dTitleIntroLinkBoyPlayerActionRow* LinkBoyPlayerActionRow = nullptr;
    const Oot3dTitleIntroOpeningPlayerMotionRow* OpeningPlayerMotionRow = nullptr;
    const Oot3dSceneCutsceneActorSnapshot* GenericActorSnapshot = nullptr;
    Oot3dTitleIntroPlayerActionTransform OpeningPlayerMotionTransform{};
    bool HasOpeningPlayerMotionTransform = false;
    bool HasMountedAttachment = false;
    bool MountedCopyParentYaw = false;
    uint16_t MountedParentActorBindingIndex = kOot3dInvalidSourceIndex;
    uint16_t MountedParentPoseOffsetNodeIndex = kOot3dInvalidSourceIndex;
    uint16_t MountedChildRootMotionNodeIndex = kOot3dInvalidSourceIndex;
    float MountedChildRootMotionScale = 0.0f;
    uint32_t MountedPlayerActionFunction = 0;
    uint32_t MountedSkeletonNodeOffsetFunction = 0;
    std::string MountedAttachmentSource = "not_mounted";
    double Frame = 0.0;
    double Interpolation = 0.0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 Position;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 Rotation;
    std::string Status = "not_sampled";
};

struct TitleIntroNativeActorCsabClip {
    bool Loaded = false;
    std::string Role;
    std::string CsabName;
    uint16_t AnimationIndex = 0;
    float PlaySpeedScale = 1.0f;
    std::vector<uint8_t> Bytes;
    ThreeDsRecomp::Oot3d::CsabMetadata Metadata;
    std::string Source = "not_loaded";
};

struct TitleIntroNativeActor {
    bool Loaded = false;
    std::string Role;
    const Oot3dTitleIntroOpeningActorBindingRow* OpeningActorBindingRow = nullptr;
    std::string OpeningActorBindingStatus = "not_resolved";
    std::filesystem::path ArchivePath;
    std::string CmbName;
    std::string CsabName;
    double Scale = 1.0;
    const Oot3dTitleIntroActorScaleRow* ScaleRow = nullptr;
    const Oot3dTitleIntroActorAnimationRow* AnimationRow = nullptr;
    const Oot3dTitleIntroActorMotionAnimationRow* MotionAnimationRow = nullptr;
    double NativeActorScale = 0.0;
    double NativeActorScaleBase = 0.0;
    std::string ScaleSource = "unresolved";
    std::string ScaleStatus = "pending_native_actor_setscale_literal";
    ThreeDsRecomp::Oot3d::CmbModel Model;
    const Oot3dTitleIntroPlayerResourceVisibilityRow* ResourceVisibilityRow = nullptr;
    std::vector<uint8_t> NativeResourceVisibility;
    uint32_t NativeVisibleResourceCount = 0;
    std::string ResourceVisibilityStatus = "not_applicable";
    bool BaseRenderModelBuilt = false;
    bool BaseRenderModelTexturePayloadsStripped = false;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel BaseRenderModel;
    std::vector<uint8_t> CsabBytes;
    ThreeDsRecomp::Oot3d::CsabMetadata Csab;
    std::vector<TitleIntroNativeActorCsabClip> MotionClips;
    std::string MotionClipStatus = "not_applicable";
    std::vector<ThreeDsRecomp::Oot3d::Matrix4f> BindWorldTransforms;
    std::string Status = "not_loaded";
};

struct TitleIntroLogoComponentRuntime {
    bool Loaded = false;
    const Oot3dTitleIntroLogoComponentRow* Row = nullptr;
    std::filesystem::path ArchivePath;
    std::string CmbName;
    std::string CsabName;
    std::string Variant = "jpeu";
    ThreeDsRecomp::Oot3d::CmbModel Model;
    std::vector<uint8_t> CsabBytes;
    ThreeDsRecomp::Oot3d::CsabMetadata Csab;
    std::vector<ThreeDsRecomp::Oot3d::CmabMaterialAnimation> MaterialAnimations;
    std::vector<std::string> MaterialAnimationNames;
    bool MaterialAnimationRuntimeLoopOverrideResolved = false;
    uint32_t MaterialAnimationRuntimeLoopMode = 0;
    uint16_t MaterialAnimationRuntimeLoopFieldOffset = 0;
    std::string MaterialAnimationRuntimeLoopSource;
    std::vector<ThreeDsRecomp::Oot3d::Matrix4f> BindWorldTransforms;
    bool BaseRenderModelBuilt = false;
    bool BaseRenderModelTexturePayloadsStripped = false;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel BaseRenderModel;
    std::string Status = "not_loaded";
};

struct TitleIntroLogoAlphaState {
    bool Decoded = false;
    bool Simulated = false;
    bool UsedForRender = false;
    bool NativeRowsUsed = false;
    double InputFrame = 0.0;
    uint32_t UpdateTicks = 0;
    uint16_t State = 0;
    uint16_t Substate = 0;
    int16_t DelayTimer = 0;
    int16_t Timer = 0;
    float TitleTextAlpha = 0.0f;
    float MainLogoAlpha = 0.0f;
    float CopyrightAlpha = 0.0f;
    float EffectAlpha = 0.0f;
    int16_t CopyrightAlphaStep = 0;
    int16_t FadeOutAlphaStep = 0;
    bool Env3Triggered = false;
    bool Env4Triggered = false;
    bool DisplayReached = false;
    bool FrameBoundaryTracePending = true;
    std::string Env3ScheduleSource = "pending_title_game_state_env_flag_schedule_decode";
    std::string Status = "not_sampled";
    std::vector<std::string> AppliedPhaseRoles;
};

struct TitleIntroLogoRuntime {
    bool Decoded = false;
    bool Loaded = false;
    const Oot3dTitleIntroOpeningActorBindingRow* OpeningActorBindingRow = nullptr;
    std::string OpeningActorBindingStatus = "not_resolved";
    const Oot3dTitleIntroActorInitSourceRow* ActorInitRow = nullptr;
    std::string Variant = "jpeu";
    std::string VariantStatus = "not_selected";
    std::filesystem::path ArchivePath;
    std::vector<TitleIntroLogoComponentRuntime> Components;
    bool DrawRouteDecoded = false;
    std::vector<const Oot3dTitleIntroLogoDrawRow*> DrawRows;
    bool DrawContextDecoded = false;
    std::vector<const Oot3dTitleIntroLogoDrawContextRow*> DrawContextRows;
    std::string DrawContextStatus = "native_enmag_draw_context_not_decoded";
    std::string DrawStatus = kOot3dTitleIntroLogoDrawProjectionBasis;
    bool UpdateRouteDecoded = false;
    std::vector<const Oot3dTitleIntroLogoUpdateRow*> UpdateRows;
    std::string UpdateStatus = "native_enmag_update_route_not_decoded";
    TitleIntroLogoAlphaState AlphaState;
    std::vector<TitleIntroLogoAlphaState> AlphaReferenceSamples;
    std::string Status = "not_loaded";
};

struct TitleIntroPerformanceTiming {
    uint64_t SampleCount = 0;
    double OpeningFrameRuntimeSeconds = 0.0;
    double RuntimeEnvironmentSeconds = 0.0;
    double RuntimeStateSeconds = 0.0;
    double EponaVisualSeconds = 0.0;
    double LinkVisualSeconds = 0.0;
    double CameraSeconds = 0.0;
    double ActorLightingSeconds = 0.0;
    double LogoDrawSeconds = 0.0;
    double BoundsSeconds = 0.0;
};

struct TitleIntroPlayback {
    bool Enabled = false;
    bool Initialized = false;
    uint16_t QdbIndex = 0;
    bool QdbIndexOverrideApplied = false;
    std::string QdbStatus = "not_resolved";
    double StartFrame = 0.0;
    double Frame = 0.0;
    double FramesPerSecond = kOot3dNativeCharacterAnimationFramesPerSecond;
    std::filesystem::path RomfsRoot;
    uint16_t OpeningOrchestrationIndex = OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;
    const Oot3dTitleIntroOpeningOrchestrationRow* OpeningOrchestrationRow = nullptr;
    std::string OpeningOrchestrationStatus = "not_resolved";
    bool GenericCutsceneRuntimeRequested = false;
    Oot3dSceneCutsceneFrameKey GenericCutsceneKey{};
    Oot3dSceneCutsceneFrameRuntimeStatus GenericCutsceneProgramStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneProgramSnapshot GenericCutsceneProgram{};
    Oot3dSceneCutsceneFrameRuntimeStatus GenericCutsceneInitStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneFrameRuntimeStatus GenericCutsceneStepStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneFrameRuntimeState GenericCutsceneState{};
    Oot3dSceneCutsceneFrameSnapshot GenericCutsceneSnapshot{};
    const Oot3dTitleIntroQdbSourceRow* QdbRow = nullptr;
    TitleIntroNativeActor LinkActor;
    TitleIntroNativeActor EponaActor;
    TitleIntroLogoRuntime LogoRuntime;
    TitleIntroCueSample LinkCue;
    TitleIntroCueSample EponaCue;
    bool LinkCueResolvedFromOpeningFrameRuntime = false;
    bool EponaCueResolvedFromOpeningFrameRuntime = false;
    bool QdbActorCueFallbackUsedForRender = false;
    const Oot3dTitleIntroQdbCommandRow* CameraCommandRow = nullptr;
    const Oot3dSceneCutsceneCameraBlobRow* CameraBlobRow = nullptr;
    const Oot3dSceneCutsceneCameraBlobSegmentRow* CameraSegmentRow = nullptr;
    uint16_t CameraBlobSegmentSourceIndex = kOot3dInvalidSourceIndex;
    const Oot3dSceneCutsceneIntroCameraCutsceneRow* InitialSceneCutsceneRow = nullptr;
    const Oot3dSceneCutsceneIntroCameraTimelineRow* InitialSceneCameraRow = nullptr;
    uint16_t InitialSceneCutsceneSourceIndex = kOot3dInvalidSourceIndex;
    double InitialSceneCutsceneFrame = 0.0;
    bool InitialSceneCameraDecoded = false;
    bool InitialSceneCameraApplied = false;
    bool QdbCameraSuppressedByInitialSceneCamera = false;
    std::string InitialSceneCameraStatus = "not_sampled";
    bool NativeCameraDecoded = false;
    bool NativeCameraApplied = false;
    std::string NativeCameraStatus = "not_sampled";
    bool OpeningFrameRuntimeInitialized = false;
    bool OpeningFrameRuntimeStepValid = false;
    bool OpeningFrameRuntimeCameraUsedForRender = false;
    bool OpeningFrameRuntimeActorMotionUsedForRender = false;
    bool OpeningFrameRuntimeLinkActorTransformUsedForRender = false;
    bool OpeningFrameRuntimeEponaActorTransformUsedForRender = false;
    bool OpeningFrameRuntimeMountedLinkAttachmentErrorValid = false;
    double OpeningFrameRuntimeMountedLinkAttachmentErrorLength = 0.0;
    bool OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw = false;
    std::string OpeningFrameRuntimeLinkActorDrawStatus =
        Oot3d_TitleIntroOpeningActorRuntimeMountedLinkDrawNotProcessedStatus();
    bool OpeningFrameRuntimeLogoDrawUsedForRender = false;
    size_t OpeningFrameRuntimeLogoDrawResolvedComponentCount = 0;
    size_t OpeningFrameRuntimeLogoDrawVisibleComponentCount = 0;
    size_t OpeningFrameRuntimeLogoDrawSubmittedVisualCount = 0;
    size_t OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount = 0;
    size_t OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount = 0;
    std::string OpeningFrameRuntimeLogoDrawRenderStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderNotProcessedStatus();
    bool OpeningFrameRuntimeLogoInputBound = false;
    uint16_t OpeningFrameRuntimeLogoInputOrchestrationIndex =
        OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;
    uint16_t OpeningFrameRuntimeLogoInputCutsceneSourceIndex = kOot3dInvalidSourceIndex;
    int32_t OpeningFrameRuntimeLogoInputCutsceneFrame = 0;
    uint8_t OpeningFrameRuntimeLogoInputEnvFlag3 = 0;
    uint8_t OpeningFrameRuntimeLogoInputEnvFlag4 = 0;
    std::string OpeningFrameRuntimeLogoInputSourceStatus = "not_sampled";
    Oot3dCutsceneIntroRuntimeStatus OpeningFrameRuntimeLogoInputInitStatus =
        OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED;
    Oot3dCutsceneIntroRuntimeStatus OpeningFrameRuntimeLogoInputStepStatus =
        OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED;
    Oot3dTitleIntroOpeningFrameRuntimeStatus OpeningFrameRuntimeInitStatus =
        OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dTitleIntroOpeningFrameRuntimeStatus OpeningFrameRuntimeStepStatus =
        OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dTitleIntroOpeningFrameRuntimeState OpeningFrameRuntimeState{};
    Oot3dTitleIntroOpeningFrameRuntimeStep OpeningFrameRuntimeStep{};
    int32_t OpeningFrameRuntimeSampledFrame = 0;
    bool RuntimeEnvironmentTimeUsedForRender = false;
    bool RuntimeEnvironmentRenderRebuilt = false;
    uint16_t RuntimeEnvironmentRenderedDayTime = 0;
    uint16_t RuntimeEnvironmentRenderedSkyboxTime = 0;
    int RuntimeEnvironmentRenderedTimeStartFrame = -1;
    bool RuntimeEnvironmentLightModeUsedForRender = false;
    int RuntimeEnvironmentRenderedLightModeCurrent = -1;
    int RuntimeEnvironmentRenderedLightModeTarget = -1;
    bool RuntimeEnvironmentRenderedLightModeBlendActive = false;
    double RuntimeEnvironmentRenderedLightModeBlendWeight = 0.0;
    uint16_t RuntimeEnvironmentRenderedLightModeBlendRemaining = 0;
    uint16_t RuntimeEnvironmentRenderedLightModeBlendDuration = 0;
    int RuntimeEnvironmentRenderedLightModeStartFrame = -1;
    bool RuntimeEnvironmentLightSettingUsedForRender = false;
    int RuntimeEnvironmentRenderedLightSettingRawIndex = -1;
    int RuntimeEnvironmentRenderedLightSettingTarget = -1;
    int RuntimeEnvironmentRenderedLightSettingSetupIndex = -1;
    int RuntimeEnvironmentRenderedLightSettingStartFrame = -1;
    bool RuntimeEnvironmentColorAddendsUsedForRender = false;
    std::array<int, 3> RuntimeEnvironmentRenderedAmbientColorAddends = { 0, 0, 0 };
    std::array<int, 3> RuntimeEnvironmentRenderedLightColorAddends = { 0, 0, 0 };
    std::array<int, 3> RuntimeEnvironmentRenderedFogColorAddends = { 0, 0, 0 };
    int RuntimeEnvironmentRenderedColorAddendSourceActionId = -1;
    bool RuntimeEnvironmentRenderedColorAddendRampActive = false;
    bool RuntimeEnvironmentMaterialAnimationFrameUsedForRender = false;
    float RuntimeEnvironmentRenderedMaterialAnimationFrame = 0.0f;
    std::string RuntimeEnvironmentMaterialAnimationFrameSource = "not_rendered";
    size_t AddedActorVisualCount = 0;
    size_t SuppressedSceneActorVisualCount = 0;
    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsRuntime NativeEffects;
    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsRenderEnvironment NativeEffectSsRenderEnvironment;
    ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustEmitterProfile HorseDustEmitterProfile;
    bool NativeEffectSsInitialized = false;
    uint64_t NativeHorseDustSpawnCount = 0;
    std::string NativeEffectSsStatus = "not_initialized";
    bool DiagnosticCameraApplied = false;
    TitleIntroPerformanceTiming Performance;
    std::string Status = "disabled";
};

struct TitleIntroActorClipSelection {
    const std::vector<uint8_t>* CsabBytes = nullptr;
    const ThreeDsRecomp::Oot3d::CsabMetadata* Csab = nullptr;
    const std::vector<uint8_t>* MorphCsabBytes = nullptr;
    const ThreeDsRecomp::Oot3d::CsabMetadata* MorphCsab = nullptr;
    std::string CsabName;
    std::string MorphCsabName;
    std::string Source = "base_title_visual_csab";
    std::string MorphSource;
    std::string MotionRole = "base";
    uint16_t AnimationIndex = kOot3dInvalidSourceIndex;
    float PlaySpeedScale = 1.0f;
    float NativeSpeed = 0.0f;
    bool PoseFrameValid = false;
    float PoseFrame = 0.0f;
    bool NativeMorphActive = false;
    float MorphPoseFrame = 0.0f;
    float MorphWeight = 0.0f;
    float MorphRate = 0.0f;
    float MorphFrames = 0.0f;
};

struct TitleIntroMountedLinkContext {
    bool RideActorRootInputValid = false;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorPosition{};
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorPoseOffset{};
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorRotation{};
    int RideActorPoseOffsetNodeIndex = -1;
    int RideActorPoseOffsetBoneIndex = -1;
    std::string Source;
};

struct TitleIntroActorVisualAppendResult {
    bool Submitted = false;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 DrawPosition{};
    bool PoseFrameValid = false;
    float PoseFrame = 0.0f;
    uint16_t AnimationIndex = kOot3dInvalidSourceIndex;
    bool NativeMorphActive = false;
    bool NativeMorphApplied = false;
    float NativeMorphWeight = 0.0f;
    float NativeMorphSourceFrame = 0.0f;
    std::string NativeMorphSourceCsabName;
    bool MountedAttachmentErrorValid = false;
    double MountedAttachmentErrorLength = 0.0;
    bool RideActorRootInputValid = false;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorPosition{};
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorPoseOffset{};
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorPoseOffsetModel{};
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 RideActorRotation{};
    int RideActorPoseOffsetNodeIndex = -1;
    int RideActorPoseOffsetBoneIndex = -1;
    bool HorseDustEmitterInputValid = false;
    std::array<bool, 4> HorseDustHoofWorldPositionValid{};
    std::array<ThreeDsRecomp::Oot3d::Oot3dDemoVec3, 4> HorseDustHoofWorldPositions{};
};

struct TitleIntroActorRenderScale {
    double Scale = 1.0;
    bool RuntimeOverrideApplied = false;
    std::string Source = "native_actor_scale";
};
