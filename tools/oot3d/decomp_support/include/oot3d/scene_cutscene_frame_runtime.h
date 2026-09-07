#ifndef OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_H
#define OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_H

#include "oot3d/title_intro_opening_frame_runtime.h"
#include "oot3d/types.h"

enum {
    OOT3D_SCENE_CUTSCENE_FRAME_ACTOR_CAPACITY = 16,
    OOT3D_SCENE_CUTSCENE_ACTOR_ANIMATION_CAPACITY = 8,
    OOT3D_SCENE_CUTSCENE_FRAME_OVERLAY_DRAW_CAPACITY = 16,
};

typedef struct Oot3dSceneCutsceneActorAnimationResource {
    u8 valid;
    u16 animationIndex;
    float playSpeedScale;
    const char* role;
    const char* csabName;
    const char* source;
} Oot3dSceneCutsceneActorAnimationResource;

typedef struct Oot3dSceneCutsceneFrameKey {
    u8 sceneId;
    u16 setupIndex;
    u16 cutsceneSourceIndex;
} Oot3dSceneCutsceneFrameKey;

typedef enum Oot3dSceneCutsceneFrameAdapterKind {
    OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_NONE = 0,
    OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_OPEN_TITLE,
    OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_SCENE_CUTSCENE,
} Oot3dSceneCutsceneFrameAdapterKind;

typedef enum Oot3dSceneCutsceneFrameRuntimeStatus {
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK = 0,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_NULL_STATE,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_NULL_OUTPUT,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_MISSING_ADAPTER,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_ADAPTER_INIT_ERROR,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_ADAPTER_STEP_ERROR,
    OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_COMPLETE,
} Oot3dSceneCutsceneFrameRuntimeStatus;

typedef struct Oot3dSceneCutsceneFrameAdapterSelection {
    Oot3dSceneCutsceneFrameKey key;
    Oot3dSceneCutsceneFrameAdapterKind kind;
    u16 adapterIndex;
} Oot3dSceneCutsceneFrameAdapterSelection;

typedef struct Oot3dSceneCutsceneActorResource {
    u8 valid;
    u16 actorBindingIndex;
    u16 actorKind;
    float actorScale;
    float gravity;
    float shadowScale;
    float focusYOffset;
    const char* actorRole;
    const char* actorName;
    const char* archivePath;
    const char* cmbName;
    const char* initCsabName;
    const char* baseCsabName;
    u16 animationResourceCount;
    Oot3dSceneCutsceneActorAnimationResource
        animationResources[OOT3D_SCENE_CUTSCENE_ACTOR_ANIMATION_CAPACITY];
} Oot3dSceneCutsceneActorResource;

typedef struct Oot3dSceneCutsceneProgramSnapshot {
    Oot3dSceneCutsceneFrameAdapterSelection adapter;
    u8 valid;
    u16 actorResourceCount;
    Oot3dSceneCutsceneActorResource
        actorResources[OOT3D_SCENE_CUTSCENE_FRAME_ACTOR_CAPACITY];
} Oot3dSceneCutsceneProgramSnapshot;

typedef struct Oot3dSceneCutsceneCameraSnapshot {
    u8 valid;
    u8 active;
    u8 held;
    u16 cameraBlobSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 timelineSourceIndex;
    s32 segmentStartFrame;
    s32 segmentEndFrame;
    const Oot3dSceneCutsceneCameraBlobRow* cameraBlob;
    const Oot3dSceneCutsceneCameraBlobSegmentRow* cameraSegment;
    Oot3dCutsceneCameraViewFrame view;
} Oot3dSceneCutsceneCameraSnapshot;

typedef struct Oot3dSceneCutsceneActorAttachmentSnapshot {
    u8 active;
    u8 copyParentYaw;
    u16 parentActorBindingIndex;
    u16 parentPoseOffsetNodeIndex;
    u16 childRootMotionNodeIndex;
    float childRootMotionScale;
    u32 mountedPlayerActionFunction;
    u32 skeletonNodeOffsetFunction;
    const char* source;
} Oot3dSceneCutsceneActorAttachmentSnapshot;

typedef struct Oot3dSceneCutsceneActorSnapshot {
    u8 valid;
    u8 active;
    u16 actorBindingIndex;
    u16 sourceKind;
    u16 actionId;
    s32 frame;
    float interpolation;
    float positionX;
    float positionY;
    float positionZ;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    float nativeSpeed;
    u8 resourceValid;
    u16 actorKind;
    float actorScale;
    const char* actorRole;
    const char* actorName;
    const char* archivePath;
    const char* cmbName;
    const char* baseCsabName;
    u8 animationValid;
    u16 animationKind;
    u16 animationIndex;
    float animationPlaySpeedScale;
    float animationNativePlaySpeed;
    float animationFrameDelta;
    u8 animationFrameValid;
    float animationFrame;
    u16 animationChangeMode;
    u16 animationUpdateMode;
    u8 animationCompletionTransitioned;
    u8 animationCompletionLooped;
    u8 animationMorphActive;
    u16 animationMorphSourceIndex;
    float animationMorphSourceFrame;
    float animationMorphWeight;
    float animationMorphRate;
    float animationMorphFrames;
    const char* animationCsabName;
    const char* animationMorphSourceCsabName;
    const char* animationMorphSource;
    const char* animationRole;
    const char* animationSource;
    Oot3dSceneCutsceneActorAttachmentSnapshot attachment;
} Oot3dSceneCutsceneActorSnapshot;

typedef struct Oot3dSceneCutsceneEnvironmentSnapshot {
    u8 timeResolved;
    u16 dayTime;
    u16 skyboxTime;
    u16 timeStartFrame;
    u8 lightModeResolved;
    u8 lightModeCurrent;
    u8 lightModeTarget;
    u8 lightModeBlendActive;
    u16 lightModeBlendRemaining;
    u16 lightModeBlendDuration;
    float lightModeBlendWeight;
    u16 lightModeStartFrame;
    u16 lightModeSourceActionId;
    u8 lightSettingResolved;
    u8 lightSettingTarget;
    u16 lightSettingRawIndex;
    u16 lightSettingSetupIndex;
    u16 lightSettingStartFrame;
    u16 lightSettingPlayTargetOffset;
    u16 lightSettingPlayBlendWeightOffset;
    const char* lightSettingScenePath;
    u8 colorAddendsResolved;
    u8 colorAddendRampActive;
    u16 colorAddendSourceActionId;
    s16 ambientColorAddends[3];
    s16 lightColorAddends[3];
    s16 fogColorAddends[3];
} Oot3dSceneCutsceneEnvironmentSnapshot;

typedef struct Oot3dSceneCutsceneOverlayDrawSnapshot {
    u8 valid;
    u8 visible;
    u16 componentIndex;
    u16 drawIndex;
    u16 alphaFieldOffset;
    u16 effectAlphaFieldOffset;
    float alphaNormalized;
    float effectAlphaNormalized;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float matrix[16];
} Oot3dSceneCutsceneOverlayDrawSnapshot;

typedef struct Oot3dSceneCutsceneFrameRuntimeState {
    Oot3dSceneCutsceneFrameAdapterSelection adapter;
    s32 frame;
    s32 nativeEndFrame;
    u8 initialized;
    u8 complete;

    /* Temporary typed adapter storage while the common snapshot is expanded. */
    Oot3dTitleIntroOpeningFrameRuntimeStatus openTitleInitStatus;
    Oot3dTitleIntroOpeningFrameRuntimeStatus openTitleStepStatus;
    Oot3dTitleIntroOpeningFrameRuntimeState openTitleState;
    Oot3dCutsceneIntroRuntimeStatus sceneCutsceneInitStatus;
    Oot3dCutsceneIntroRuntimeStatus sceneCutsceneStepStatus;
    Oot3dCutsceneIntroRuntimeState sceneCutsceneState;
} Oot3dSceneCutsceneFrameRuntimeState;

typedef struct Oot3dSceneCutsceneFrameSnapshot {
    Oot3dSceneCutsceneFrameAdapterSelection adapter;
    s32 frame;
    s32 nativeEndFrame;
    u8 valid;
    u8 complete;
    Oot3dSceneCutsceneCameraSnapshot camera;
    u16 actorCount;
    Oot3dSceneCutsceneActorSnapshot actors[OOT3D_SCENE_CUTSCENE_FRAME_ACTOR_CAPACITY];
    Oot3dSceneCutsceneEnvironmentSnapshot environment;
    u8 overlayValid;
    u16 overlayDrawCount;
    Oot3dSceneCutsceneOverlayDrawSnapshot
        overlayDraws[OOT3D_SCENE_CUTSCENE_FRAME_OVERLAY_DRAW_CAPACITY];

    /* Migration payload; consumers must dispatch using adapter.kind. */
    Oot3dTitleIntroOpeningFrameRuntimeStatus openTitleStepStatus;
    Oot3dTitleIntroOpeningFrameRuntimeStep openTitleStep;
    Oot3dCutsceneIntroRuntimeStatus sceneCutsceneStepStatus;
    Oot3dCutsceneIntroRuntimeStep sceneCutsceneStep;
} Oot3dSceneCutsceneFrameSnapshot;

Oot3dSceneCutsceneFrameRuntimeStatus Oot3d_SceneCutsceneFrameRuntimeResolveAdapter(
    Oot3dSceneCutsceneFrameKey key,
    Oot3dSceneCutsceneFrameAdapterSelection* outSelection
);

Oot3dSceneCutsceneFrameRuntimeStatus Oot3d_SceneCutsceneFrameRuntimeInit(
    Oot3dSceneCutsceneFrameRuntimeState* state,
    Oot3dSceneCutsceneFrameKey key
);

Oot3dSceneCutsceneFrameRuntimeStatus Oot3d_SceneCutsceneFrameRuntimeBuildProgram(
    Oot3dSceneCutsceneFrameKey key,
    Oot3dSceneCutsceneProgramSnapshot* outProgram
);

Oot3dSceneCutsceneFrameRuntimeStatus Oot3d_SceneCutsceneFrameRuntimeStep(
    Oot3dSceneCutsceneFrameRuntimeState* state,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dSceneCutsceneFrameSnapshot* outSnapshot
);

const char* Oot3d_SceneCutsceneFrameAdapterKindName(Oot3dSceneCutsceneFrameAdapterKind kind);
const char* Oot3d_SceneCutsceneFrameRuntimeStatusName(Oot3dSceneCutsceneFrameRuntimeStatus status);

#endif
