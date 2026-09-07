#ifndef OOT3D_SCENE_CUTSCENE_INTRO_RUNTIME_H
#define OOT3D_SCENE_CUTSCENE_INTRO_RUNTIME_H

#include "oot3d/scene_cutscene_intro_camera_timeline.h"
#include "oot3d/scene_cutscene_direct_player_event_table.h"
#include "oot3d/scene_cutscene_lighting_table.h"
#include "oot3d/scene_cutscene_misc_action_table.h"
#include "oot3d/scene_cutscene_native_source_table.h"
#include "oot3d/scene_cutscene_player_action_table.h"
#include "oot3d/scene_cutscene_set_time_table.h"
#include "oot3d/scene_cutscene_transition_handoff_table.h"
#include "oot3d/scene_global_entrance.h"

enum {
    OOT3D_CUTSCENE_INTRO_STATE_RUNNING = 0,
    OOT3D_CUTSCENE_INTRO_STATE_END_REQUESTED = 3,
    OOT3D_CUTSCENE_INTRO_STATE_DIRECT_PLAYER_EVENT_DISPATCHED = 4,
    OOT3D_CUTSCENE_INTRO_TRANSITION_COUNTER_SMALL_LIMIT = 0x0080,
    OOT3D_CUTSCENE_INTRO_TRANSITION_COUNTER_TIMED_LIMIT = 0x0672,
    OOT3D_CUTSCENE_INTRO_LIGHT_MODE_BLEND_0_TO_1_FRAMES = 0x003C,
    OOT3D_CUTSCENE_INTRO_LIGHT_MODE_0 = 0,
    OOT3D_CUTSCENE_INTRO_LIGHT_MODE_1 = 1,
    OOT3D_CUTSCENE_INDEX_LAYER_BASE = 0xFFF0,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_DAY_START = 0x4555,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_DAY_LENGTH = 0x7AAC,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_SKYBOX_WRAP_THRESHOLD = 0x0AAB,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_NIGHT_DOUBLE_LIMIT = 0x0190,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_DEFAULT = 10,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_DEFAULT = 1,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_BOOT_VALUE = 2,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_INIT_ADDRESS = 0x00450B68,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_STORE_ADDRESS = 0x00450B80,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_POINTER_ADDRESS = 0x0051B2F4,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_GAME_SPEED_FIELD_OFFSET = 0x0110,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_SOURCE_ADDRESS = 0x00531EBE,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_INIT_FUNCTION = 0x00450B60,
    OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_UPDATE_FUNCTION = 0x0045DD50,
};

typedef enum {
    OOT3D_CUTSCENE_INTRO_RUNTIME_OK = 0,
    OOT3D_CUTSCENE_INTRO_RUNTIME_NULL_STATE,
    OOT3D_CUTSCENE_INTRO_RUNTIME_NULL_OUTPUT,
    OOT3D_CUTSCENE_INTRO_RUNTIME_UNKNOWN_CUTSCENE,
    OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED,
    OOT3D_CUTSCENE_INTRO_RUNTIME_COMPLETE,
} Oot3dCutsceneIntroRuntimeStatus;

typedef struct {
    u16 cutsceneSourceIndex;
    s32 frame;
    s32 nativeEndFrame;
    u32 cutsceneEndCounter;
    s16 transitionCounter53F0;
    u8 initialized;
    u8 complete;
    u8 cutsceneState;
    u8 cameraDataIndex;
    u8 envFlag3;
    u8 envFlag4;
    u8 envFlag3271;
    u8 environmentLightSettingResolved;
    u8 environmentLightSettingTarget;
    u16 environmentLightSettingRawIndex;
    u16 environmentLightSettingStartFrame;
    u16 environmentLightSettingPlayTargetOffset;
    u16 environmentLightSettingPlayBlendWeightOffset;
    const Oot3dSceneCutsceneLightingRow* environmentLightSettingRow;
    u8 firstFrameResetApplied;
    u8 directPlayerEventDispatched;
    u8 directPlayerEventDynamicState;
    u8 directPlayerEventDynamicStateResolved;
    u32 directPlayerEventDynamicStateAddress;
    u8 transitionRequestPending;
    u16 transitionRequestIndex;
    u8 transitionRequestTrigger;
    u8 transitionRequestEffect;
    const Oot3dGlobalEntranceRow* transitionRequestEntrance;
    u8 transitionRequestEntranceResolved;
    const Oot3dSceneCutsceneTransitionHandoffRow* transitionRequestHandoff;
    u8 transitionRequestHandoffResolved;
    const Oot3dSceneCutsceneNativeSourceRow* transitionRequestCutscene;
    u8 transitionRequestCutsceneResolved;
    u8 transitionRequestSetupIndexResolved;
    u16 transitionRequestSetupIndex;
    u32 transitionRequestCutsceneIndexValue;
    u8 transitionRequestCutsceneIndexResolved;
    u32 playerRuntimeField8Value;
    u8 playerRuntimeField8ValueResolved;
    u16 playerByteWriteOffset;
    u8 playerByteWriteValue;
    u8 playerByteWriteResolved;
    u16 itemGiveId;
    u8 itemGiveResolved;
    u16 dayTime;
    u16 skyboxTime;
    u8 timeResolved;
    u8 timeHour;
    u8 timeMinute;
    u16 timeStartFrame;
    const Oot3dSceneCutsceneSetTimeRow* setTimeRow;
    u8 nativeTimeProgressionResolved;
    u8 nativeTimeNightFlag;
    u8 nativeTimeOutputMirrorApplied;
    u16 nativeTimeRate;
    u16 nativeTimeGameSpeed;
    u16 nativeTimeBaseIncrement;
    u16 nativeTimeDelta;
    u16 nativeTimePreviousDayTime;
    u16 nativeTimePreviousSkyboxTime;
    u8 lightModeResolved;
    u8 lightModeCurrent;
    u8 lightModeTarget;
    u8 lightModeBlendActive;
    u16 lightModeBlendRemaining;
    u16 lightModeBlendDuration;
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
} Oot3dCutsceneIntroRuntimeState;

typedef struct {
    s32 frame;
    const Oot3dSceneCutsceneNativeSourceRow* nativeCutscene;
    const Oot3dSceneCutsceneIntroCameraTimelineRow* cameraRow;
    const Oot3dSceneCutscenePlayerActionRow* activePlayerAction;
    const Oot3dSceneCutscenePlayerActionRow* startPlayerAction;
    const Oot3dSceneCutsceneDirectPlayerEventRow* startDirectPlayerEvent;
    const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow* startDirectPlayerEventDynamicVariant;
    const Oot3dSceneCutsceneLightingRow* startLighting;
    Oot3dCutsceneIntroCameraStatus cameraStatus;
    Oot3dCutsceneCameraViewFrame view;
    u32 activePlayerActionCount;
    u32 startTriggerPlayerActionCount;
    u32 startDirectPlayerEventCount;
    u32 startLightingCount;
    u32 activeMiscActionCount;
    u32 startTriggerActionCount;
    u8 firstFrameReset;
    u8 requestCutsceneEnd;
    u8 requestCameraDataIndex0;
    u8 transitionCounterSmallRampActive;
    u8 environmentFlag3271To10;
    u8 environmentFlag3Set;
    u8 environmentFlag4Set;
    u8 environmentLightSettingApplied;
    u8 environmentLightSettingResolved;
    u8 environmentLightSettingTarget;
    u16 environmentLightSettingRawIndex;
    u16 environmentLightSettingStartFrame;
    u16 environmentLightSettingPlayTargetOffset;
    u16 environmentLightSettingPlayBlendWeightOffset;
    u8 transitionCounterTimedSfxActive;
    u8 cameraQuakeStartRequested;
    u8 cameraQuakeStopRequested;
    u8 directPlayerEventDispatchApplied;
    u8 directPlayerEventDynamicVariantApplied;
    u8 directPlayerEventDynamicStateBefore;
    u8 directPlayerEventDynamicStateAfter;
    u32 directPlayerEventDynamicStateAddress;
    u8 requestDirectPlayerEventEndState;
    u8 transitionRequestApplied;
    u16 transitionRequestIndex;
    u8 transitionRequestTrigger;
    u8 transitionRequestEffect;
    const Oot3dGlobalEntranceRow* transitionRequestEntrance;
    u8 transitionRequestEntranceResolved;
    const Oot3dSceneCutsceneTransitionHandoffRow* transitionRequestHandoff;
    u8 transitionRequestHandoffResolved;
    const Oot3dSceneCutsceneNativeSourceRow* transitionRequestCutscene;
    u8 transitionRequestCutsceneResolved;
    u8 transitionRequestSetupIndexResolved;
    u16 transitionRequestSetupIndex;
    u32 transitionRequestCutsceneIndexValue;
    u8 transitionRequestCutsceneIndexResolved;
    u32 playerRuntimeField8Value;
    u8 playerRuntimeField8ValueResolved;
    u16 playerByteWriteOffset;
    u8 playerByteWriteValue;
    u8 playerByteWriteResolved;
    u16 itemGiveId;
    u8 itemGiveResolved;
    const Oot3dSceneCutsceneSetTimeRow* startSetTime;
    u32 startSetTimeCount;
    u8 setTimeApplied;
    u16 dayTime;
    u16 skyboxTime;
    u8 timeResolved;
    u8 timeHour;
    u8 timeMinute;
    u16 timeStartFrame;
    u8 nativeTimeProgressionApplied;
    u8 nativeTimeNightFlagBefore;
    u8 nativeTimeNightFlagAfter;
    u8 nativeTimeOutputMirrorApplied;
    u16 nativeTimeRate;
    u16 nativeTimeGameSpeed;
    u16 nativeTimeBaseIncrement;
    u16 nativeTimeDelta;
    u16 nativeTimeDayTimeBefore;
    u16 nativeTimeDayTimeAfter;
    u16 nativeTimeSkyboxTimeBefore;
    u16 nativeTimeSkyboxTimeAfter;
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
} Oot3dCutsceneIntroRuntimeStep;

const Oot3dSceneCutsceneNativeSourceRow* Oot3d_CutsceneIntroRuntimeFindNativeCutscene(
    u16 cutsceneSourceIndex
);

const Oot3dSceneCutsceneNativeSourceRow* Oot3d_CutsceneIntroRuntimeFindNativeCutsceneForTransition(
    const Oot3dGlobalEntranceRow* entrance,
    u32 cutsceneIndexValue,
    u16* outSetupIndex
);

Oot3dCutsceneIntroRuntimeStatus Oot3d_CutsceneIntroRuntimeInit(
    Oot3dCutsceneIntroRuntimeState* state,
    u16 cutsceneSourceIndex
);

Oot3dCutsceneIntroRuntimeStatus Oot3d_CutsceneIntroRuntimeStep(
    Oot3dCutsceneIntroRuntimeState* state,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dCutsceneIntroRuntimeStep* outStep
);

#endif
