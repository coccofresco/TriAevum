#ifndef OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_H

#include "oot3d/title_intro_cutscene_runtime.h"
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_camera_runtime.h"
#include "oot3d/title_intro_opening_logo_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
#include "oot3d/scene_cutscene_set_time_table.h"
#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_RECORD_BYTE_TABLE_CAPACITY \
    (OOT3D_TITLE_INTRO_RECORD_BYTE_SIDE_EFFECT_LIMIT + 2u)

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK = 0,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_NULL_STATE,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_MISSING_ORCHESTRATION,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_CAMERA_ERROR,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_ACTOR_ERROR,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_CUTSCENE_ERROR,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_LOGO_ERROR,
    OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_COMPLETE,
} Oot3dTitleIntroOpeningFrameRuntimeStatus;

typedef struct Oot3dTitleIntroOpeningFrameRuntimeState {
    u16 orchestrationIndex;
    s32 frame;
    s32 nativeEndFrame;
    s32 cameraSegmentStartFrame;
    s32 cameraSegmentEndFrame;
    u8 cameraViewValid;
    Oot3dTitleIntroOpeningCameraSample cameraView;
    u8 initialized;
    u8 complete;
    Oot3dTitleIntroOpeningPairedMountMotionState pairedMountMotionState;
    Oot3dTitleIntroOpeningLogoState logoState;
    u8 recordByteTableBytes[OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_RECORD_BYTE_TABLE_CAPACITY];
    Oot3dTitleIntroNativeInputContext nativeInputContext;
    Oot3dTitleIntroCutsceneRuntimeState cutsceneRuntime;
    Oot3dTitleIntroDispatchState dispatchState;
    Oot3dCutsceneIntroRuntimeState logoInputCutsceneRuntime;
    Oot3dCutsceneIntroRuntimeStep logoInputCutsceneStep;
    Oot3dCutsceneIntroRuntimeStatus logoInputInitStatus;
    Oot3dCutsceneIntroRuntimeStatus logoInputStepStatus;
    u16 logoInputCutsceneSourceIndex;
    u8 logoInputBound;
    u8 logoInputTransitionHandoffApplied;
    u16 logoInputPreviousCutsceneSourceIndex;
    u8 logoInputEnvFlag3;
    u8 logoInputEnvFlag4;
    const Oot3dSceneCutsceneSetTimeRow* setTimeRow;
    u16 dayTime;
    u16 skyboxTime;
    u16 timeStartFrame;
    u8 timeResolved;
    u8 timeHour;
    u8 timeMinute;
    u8 lightModeResolved;
    u8 lightModeCurrent;
    u8 lightModeTarget;
    u8 lightModeBlendActive;
    u16 lightModeBlendRemaining;
    u16 lightModeBlendDuration;
    float lightModeBlendWeight;
    u16 lightModeStartFrame;
    u16 lightModeSourceActionId;
    const Oot3dSceneCutsceneMiscActionRow* lightModeActionRow;
    u8 colorAddendsResolved;
    u8 colorAddendRampActive;
    u16 colorAddendSourceActionId;
    const Oot3dSceneCutsceneMiscActionRow* colorAddendActionRow;
    s16 ambientColorAddends[3];
    s16 lightColorAddends[3];
    s16 fogColorAddends[3];
    u8 environmentLightSettingResolved;
    u8 environmentLightSettingTarget;
    u16 environmentLightSettingRawIndex;
    u16 environmentLightSettingStartFrame;
    u16 environmentLightSettingPlayTargetOffset;
    u16 environmentLightSettingPlayBlendWeightOffset;
    const Oot3dSceneCutsceneLightingRow* environmentLightSettingRow;
} Oot3dTitleIntroOpeningFrameRuntimeState;

typedef struct Oot3dTitleIntroOpeningFrameRuntimeStep {
    u16 orchestrationIndex;
    s32 frame;
    s32 nativeEndFrame;
    s32 cameraSegmentStartFrame;
    s32 cameraSegmentEndFrame;
    u8 cameraSegmentActive;
    u8 cameraViewAvailable;
    u8 cameraViewHeld;
    u8 complete;
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration;
    const Oot3dSceneCutsceneCameraBlobSegmentRow* cameraSegment;
    Oot3dTitleIntroOpeningCameraRuntimeStatus cameraStatus;
    Oot3dTitleIntroOpeningCameraSample camera;
    Oot3dTitleIntroOpeningActorSampleStatus actorMotionStatus;
    u8 actorMotionActive;
    Oot3dTitleIntroOpeningActorMotionSample actorMotion;
    u8 cutsceneQdbIndexBound;
    u16 cutsceneQdbIndex;
    const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow* directRecordLayout;
    u8 directRecordLayoutActive;
    u8 directNativeInputCaptureApplied;
    Oot3dTitleIntroSourceSelectorFeedStatus directNativeInputCaptureStatus;
    Oot3dTitleIntroNativeInputCaptureEvent directNativeInputCaptureEvent;
    u8 directModeOpenApplied;
    Oot3dTitleIntroSourceSelectorFeedStatus directModeOpenStatus;
    Oot3dTitleIntroSourceSelectorFeedEvent directModeOpenEvent;
    u8 directGlobalGateOpenApplied;
    Oot3dTitleIntroSourceSelectorFeedStatus directGlobalGateOpenStatus;
    Oot3dTitleIntroSourceSelectorFeedEvent directGlobalGateOpenEvent;
    u8 directModeResetApplied;
    Oot3dTitleIntroSourceSelectorFeedStatus directModeResetStatus;
    Oot3dTitleIntroSourceSelectorFeedEvent directModeResetEvent;
    u8 cutsceneRuntimeApplied;
    Oot3dTitleIntroPlaybackStatus cutsceneRuntimeStatus;
    Oot3dTitleIntroCutsceneRuntimeStep cutsceneRuntime;
    const Oot3dSceneCutsceneSetTimeRow* startSetTime;
    u32 startSetTimeCount;
    u8 setTimeApplied;
    u16 dayTime;
    u16 skyboxTime;
    u16 timeStartFrame;
    u8 timeResolved;
    u8 timeHour;
    u8 timeMinute;
    u8 lightModeResolved;
    u8 lightModeCurrent;
    u8 lightModeTarget;
    u8 lightModeBlendActive;
    u8 lightModeBlendStartApplied;
    u16 lightModeBlendRemaining;
    u16 lightModeBlendDuration;
    float lightModeBlendWeight;
    u16 lightModeStartFrame;
    u16 lightModeSourceActionId;
    const Oot3dSceneCutsceneMiscActionRow* lightModeActionRow;
    u8 colorAddendsResolved;
    u8 colorAddendRampActive;
    u16 colorAddendSourceActionId;
    const Oot3dSceneCutsceneMiscActionRow* colorAddendActionRow;
    s16 ambientColorAddends[3];
    s16 lightColorAddends[3];
    s16 fogColorAddends[3];
    u8 environmentLightSettingApplied;
    u8 environmentLightSettingResolved;
    u8 environmentLightSettingTarget;
    u16 environmentLightSettingRawIndex;
    u16 environmentLightSettingStartFrame;
    u16 environmentLightSettingPlayTargetOffset;
    u16 environmentLightSettingPlayBlendWeightOffset;
    const Oot3dSceneCutsceneLightingRow* environmentLightSettingRow;
    u8 logoInputBound;
    u8 logoInputTransitionHandoffApplied;
    u16 logoInputPreviousCutsceneSourceIndex;
    u16 logoInputCutsceneSourceIndex;
    s32 logoInputCutsceneFrame;
    u8 logoInputEnvFlag3;
    u8 logoInputEnvFlag4;
    Oot3dCutsceneIntroRuntimeStatus logoInputInitStatus;
    Oot3dCutsceneIntroRuntimeStatus logoInputStepStatus;
    Oot3dCutsceneIntroRuntimeStep logoInputCutsceneStep;
    Oot3dTitleIntroOpeningLogoStepInput logoInput;
    Oot3dTitleIntroOpeningLogoRuntimeStatus logoStepStatus;
    u8 logoStepApplied;
    Oot3dTitleIntroOpeningLogoStepResult logoStep;
    Oot3dTitleIntroOpeningLogoRuntimeStatus logoDrawStatus;
    Oot3dTitleIntroOpeningLogoDrawSnapshot logoDraw;
} Oot3dTitleIntroOpeningFrameRuntimeStep;

Oot3dTitleIntroOpeningFrameRuntimeStatus Oot3d_TitleIntroOpeningFrameRuntimeInit(
    Oot3dTitleIntroOpeningFrameRuntimeState* state,
    u16 orchestrationIndex,
    u8 forceRisingButtonLogoState
);

Oot3dTitleIntroOpeningFrameRuntimeStatus Oot3d_TitleIntroOpeningFrameRuntimeStep(
    Oot3dTitleIntroOpeningFrameRuntimeState* state,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dTitleIntroOpeningFrameRuntimeStep* outStep
);

const char* Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceKind(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state,
    const Oot3dTitleIntroOpeningFrameRuntimeStep* step
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceStatus(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state,
    const Oot3dTitleIntroOpeningFrameRuntimeStep* step
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceKind(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceStatus(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceKind(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceStatus(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceKind(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceStatus(
    const Oot3dTitleIntroOpeningFrameRuntimeState* state
);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLogoInputNotSampledStatus(void);
const char* Oot3d_TitleIntroOpeningFrameRuntimeLogoInputSourceStatus(u8 logoInputBound);
const char* Oot3d_TitleIntroOpeningFrameRuntimeStepNotValidStatus(void);
const char* Oot3d_TitleIntroOpeningFrameRuntimeMaterialAnimationFrameSource(u8 openingFrameRuntimeSampled);

#endif
