#ifndef OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_H

#include "oot3d/title_intro_opening_player_motion.h"
#include "oot3d/title_intro_source_table.h"
#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX 0xFFFFu
#define OOT3D_TITLE_INTRO_OPENING_EPONA_MOUNT_CUE_COMMAND_ID 0x0000003Eu

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_LINK_ADULT = 1,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_EPONA = 2,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_TITLE_LOGO = 3,
} Oot3dTitleIntroOpeningActorKind;

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK = 0,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NO_ACTIVE_CUE,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_MOTION_ERROR,
} Oot3dTitleIntroOpeningActorSampleStatus;

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_NONE = 0,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_LINK_PLAYER_ACTION,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_PAIRED_MOUNT_CUE,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_PAIRED_MOUNT_PLAYER_ACTION,
} Oot3dTitleIntroOpeningActorVisualTransformSource;

typedef struct Oot3dTitleIntroOpeningActorMountAttachment {
    u8 active;
    u8 copyParentYaw;
    u16 parentActorBindingIndex;
    u16 parentPoseOffsetNodeIndex;
    u16 childRootMotionNodeIndex;
    float childRootMotionScale;
    u32 mountedPlayerActionFunction;
    u32 skeletonNodeOffsetFunction;
    const char* source;
} Oot3dTitleIntroOpeningActorMountAttachment;

typedef struct Oot3dTitleIntroOpeningActorBindingRow {
    u16 actorBindingIndex;
    u16 orchestrationIndex;
    u16 actorKind;
    u16 sourceScaleIndex;
    u16 sourceAnimationIndex;
    u16 sourceMotionAnimationIndex;
    u16 sourceActorInitIndex;
    u16 pairedActorBindingIndex;
    u16 requiredAssetRefIndex;
    u16 logoComponentRefStart;
    u16 logoComponentRefCount;
    u16 logoDrawRefStart;
    u16 logoDrawRefCount;
    u16 logoUpdateRefStart;
    u16 logoUpdateRefCount;
    u32 actorInitFunction;
    u32 actorUpdateFunction;
    u32 actorDrawFunction;
    float actorScale;
    float gravity;
    float shadowScale;
    float focusYOffset;
    const char* actorRole;
    const char* actorName;
    const char* archivePath;
    const char* cmbName;
    const char* initCsabName;
    const char* titleVisualCsabName;
    const char* runtimeBindingRole;
    const char* nativeBasis;
    const char* unresolved;
} Oot3dTitleIntroOpeningActorBindingRow;

typedef struct Oot3dTitleIntroOpeningActorDirectRecordLayoutRow {
    u16 directRecordLayoutIndex;
    u16 entryDirectStateId;
    u16 consumerDirectStateId;
    u16 completionDirectStateId;
    u16 branchDirectStateId;
    u32 entryModeChangeFunction;
    u8 entryModeByte;
    u32 entryGlobalGateFunction;
    u8 entryGlobalGateByte;
    u32 state37HandlerFunction;
    u32 recordPointerGetterFunction;
    u32 recordPointerLiteralPoolAddress;
    u32 recordContextAddress;
    u32 recordTableAddress;
    u32 recordPointerAddress;
    u16 recordContextOffset;
    u16 recordStride;
    u8 recordByte0Offset;
    u8 recordByte1Offset;
    u8 recordByte2Offset;
    const char* recordByte0Role;
    const char* recordByte1Role;
    const char* recordByte2Role;
    const char* recordByte0Source;
    const char* recordByte1Source;
    const char* recordByte2Source;
    u32 recordApplyFunction;
    u32 recordByteTableLiteralPoolAddress;
    u32 recordByteTableAddress;
    u32 firstChangeLatchAddress;
    u32 firstChangeInitContextAddress;
    u32 recordChangeNotifyContextAddress;
    u16 recordApplySideEffectIndexLimit;
    u32 state37ApplyCurrentCallsite;
    u32 state37ApplyTerminatorCallsite;
    u16 state37PlayerRecordPointerOffset;
    u16 state37PlayerLastRecordValueOffset;
    const char* state37CursorSource;
    const char* recordPointerStatus;
    const char* recordApplyStatus;
    const char* recordCRuntimeStatus;
    const char* state37PlaybackRuntimeStatus;
    const char* sourceEvidence;
    const char* unresolved;
} Oot3dTitleIntroOpeningActorDirectRecordLayoutRow;

typedef struct Oot3dTitleIntroOpeningActorMotionCueRow {
    u16 timelineIndex;
    u16 orchestrationIndex;
    u16 actorBindingIndex;
    u16 pairedMountActorBindingIndex;
    u16 motionRefIndex;
    u16 playerActionRefIndex;
    u16 playerActionSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u16 actionId;
    u16 startFrame;
    u16 endFrame;
    u16 durationFrames;
    u8 directStateCasePresent;
    u8 notDirectStateCase;
    u8 hasQuantizedMotionVector;
    u8 speedClamped;
    u8 startExclusiveEndInclusive;
    u16 directRecordLayoutIndex;
    u16 directStateId;
    u32 directStateSwitchFunction;
    u32 directStateSwitchWrapperFunction;
    u32 directStateSetterFunction;
    u32 directStateHandlerFunction;
    float nativeSpeed;
    s16 nativeHeading;
    const char* actionIdHex;
    const char* directStateIdHex;
    const char* directStateFeatures;
    const char* directStateCalls;
    const char* directStateOutgoingTargets;
    const char* playerActionConsumerStatus;
    const char* directStateEvidence;
    const char* sourceSemantic;
    const char* unresolved;
} Oot3dTitleIntroOpeningActorMotionCueRow;

typedef struct Oot3dTitleIntroOpeningActorVisualTransformSample {
    u8 valid;
    u8 active;
    u16 sourceKind;
    u16 actorBindingIndex;
    s32 frame;
    float interpolation;
    float positionX;
    float positionY;
    float positionZ;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    const Oot3dTitleIntroOpeningPlayerMotionRow* motionRow;
    const Oot3dTitleIntroActorCueRow* actorCueRow;
} Oot3dTitleIntroOpeningActorVisualTransformSample;

typedef struct Oot3dTitleIntroOpeningPairedMountMotionState {
    u8 initialized;
    u8 motionStateId;
    u8 positionResetThisFrame;
    u8 sourceBacked;
    u8 animationInitialized;
    u8 animationFrameValid;
    u8 animationCompletionTransitioned;
    u8 animationCompletionLooped;
    u8 animationMorphActive;
    u8 animationSubstate;
    u16 animationActionId;
    u16 animationIndex;
    u16 animationMorphSourceIndex;
    u16 animationChangeMode;
    u16 animationUpdateMode;
    float animationFrame;
    float animationPlaySpeed;
    float animationMorphSourceFrame;
    float animationMorphWeight;
    float animationMorphRate;
    float animationMorphFrames;
    const char* animationMorphSourceCsabName;
    const Oot3dTitleIntroHorseCutsceneActionRouteRow* animationRoute;
    float positionX;
    float positionY;
    float positionZ;
    float speed;
    s16 yaw;
} Oot3dTitleIntroOpeningPairedMountMotionState;

typedef struct Oot3dTitleIntroOpeningPairedMountAnimationSample {
    u8 valid;
    u8 frameValid;
    u8 completionTransitioned;
    u8 completionLooped;
    u8 morphActive;
    u16 actionId;
    u16 animationIndex;
    u16 morphSourceAnimationIndex;
    u16 changeMode;
    u16 updateMode;
    float frame;
    float playSpeed;
    float morphSourceFrame;
    float morphWeight;
    float morphRate;
    float morphFrames;
    const char* csabName;
    const char* morphSourceCsabName;
    const char* morphSource;
    const char* motionRole;
    const char* source;
    const Oot3dTitleIntroHorseCutsceneActionRouteRow* route;
} Oot3dTitleIntroOpeningPairedMountAnimationSample;

typedef struct Oot3dTitleIntroOpeningActorCueSample {
    u8 valid;
    u8 active;
    u16 qdbIndex;
    u32 commandId;
    float frame;
    float interpolation;
    float positionX;
    float positionY;
    float positionZ;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    const Oot3dTitleIntroActorCueRow* row;
    const char* status;
} Oot3dTitleIntroOpeningActorCueSample;

typedef struct Oot3dTitleIntroOpeningActorMotionSample {
    s32 frame;
    u8 active;
    u16 timelineIndex;
    u16 actorBindingIndex;
    u16 pairedMountActorBindingIndex;
    u16 motionRefIndex;
    u16 playerActionRefIndex;
    u16 playerActionSourceIndex;
    u16 actionId;
    float nativeSpeed;
    s16 nativeHeading;
    const Oot3dTitleIntroOpeningActorBindingRow* actorBinding;
    const Oot3dTitleIntroOpeningActorBindingRow* pairedMountBinding;
    const Oot3dTitleIntroOpeningActorMotionCueRow* cue;
    const Oot3dTitleIntroOpeningPlayerMotionRow* motionRow;
    Oot3dTitleIntroPlayerActionTransform playerActionTransform;
    const Oot3dTitleIntroLinkBoyPlayerActionRow* playerActionRow;
    const Oot3dTitleIntroActorCueRow* pairedMountCueRow;
    Oot3dTitleIntroOpeningActorMountAttachment linkMountAttachment;
    Oot3dTitleIntroOpeningActorVisualTransformSample linkVisualTransform;
    Oot3dTitleIntroOpeningActorVisualTransformSample pairedMountVisualTransform;
    Oot3dTitleIntroOpeningPairedMountAnimationSample pairedMountAnimation;
    Oot3dTitleIntroPlayerActionMotionResult motion;
} Oot3dTitleIntroOpeningActorMotionSample;

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_BASE_TITLE_VISUAL = 0,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_LINK_PLAYER_ACTION_TITLE_VISUAL,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_PAIRED_MOUNT_TITLE_VISUAL,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_HORSE_CUTSCENE_ACTION_ROUTE,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_MOUNTED_GALLOP_ROUTE,
    OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_SPEED_STATE_ROUTE,
} Oot3dTitleIntroOpeningActorClipSelectionKind;

typedef struct Oot3dTitleIntroOpeningActorClipRuntimeSelection {
    u8 valid;
    u16 kind;
    u16 animationIndex;
    float nativeSpeed;
    float playSpeedScale;
    u8 animationFrameValid;
    float animationFrame;
    u8 animationMorphActive;
    u16 animationMorphSourceIndex;
    float animationMorphSourceFrame;
    float animationMorphWeight;
    float animationMorphRate;
    float animationMorphFrames;
    const char* csabName;
    const char* animationMorphSourceCsabName;
    const char* animationMorphSource;
    const char* clipRole;
    const char* motionRole;
    const char* sourceKind;
    const Oot3dTitleIntroHorseStateRouteRow* gallopRoute;
    const Oot3dTitleIntroHorseCutsceneActionRouteRow* horseCutsceneRoute;
    const Oot3dTitleIntroOpeningPlayerMotionRow* motionRow;
} Oot3dTitleIntroOpeningActorClipRuntimeSelection;

extern const Oot3dTitleIntroOpeningActorBindingRow gOot3dTitleIntroOpeningActorBindingRows[];
extern const u32 gOot3dTitleIntroOpeningActorBindingRowCount;
extern const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[];
extern const u32 gOot3dTitleIntroOpeningActorDirectRecordLayoutRowCount;
extern const Oot3dTitleIntroOpeningActorMotionCueRow gOot3dTitleIntroOpeningActorMotionCueRows[];
extern const u32 gOot3dTitleIntroOpeningActorMotionCueRowCount;

const Oot3dTitleIntroOpeningActorBindingRow* Oot3d_TitleIntroOpeningActorRuntimeGetBinding(u16 actorBindingIndex);
const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow* Oot3d_TitleIntroOpeningActorRuntimeGetDirectRecordLayout(u16 directRecordLayoutIndex);
const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionCue(u16 timelineIndex);
const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindActivePlayerMotionCue(s32 frame);
const Oot3dTitleIntroLinkBoyPlayerActionRow* Oot3d_TitleIntroOpeningActorRuntimeGetPlayerActionSourceRow(u16 playerActionRefIndex);
const Oot3dTitleIntroActorCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindPairedMountCueSourceRow(
    const Oot3dTitleIntroLinkBoyPlayerActionRow* playerActionRow,
    s32 frame
);
const Oot3dTitleIntroActorScaleRow* Oot3d_TitleIntroOpeningActorRuntimeGetScaleSourceRow(u16 actorBindingIndex);
const Oot3dTitleIntroActorAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetAnimationSourceRow(u16 actorBindingIndex);
const Oot3dTitleIntroActorMotionAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionAnimationSourceRow(u16 actorBindingIndex);
const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningActorRuntimeGetActorInitSourceRow(u16 actorBindingIndex);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsMotionAnimationRowDecoded(
    const Oot3dTitleIntroActorMotionAnimationRow* row
);
const char* Oot3d_TitleIntroOpeningActorRuntimeSpeedStateMotionClipSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedGallopMotionClipSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMotionClipMissingFallbackSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMotionClipLoadStatus(
    u8 motionAnimationRowDecoded,
    u8 allExpectedClipsLoaded,
    u8 mountedGallopCarrotLoaded
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsScaleRowDecoded(
    const Oot3dTitleIntroActorScaleRow* row
);
const char* Oot3d_TitleIntroOpeningActorRuntimeScaleSource(u8 decoded);
const char* Oot3d_TitleIntroOpeningActorRuntimeScaleStatus(u8 decoded);
const char* Oot3d_TitleIntroOpeningActorRuntimeActorInitScaleSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeAnimationRowStatus(
    const Oot3dTitleIntroActorAnimationRow* row
);
const char* Oot3d_TitleIntroOpeningActorRuntimeArchiveMissingStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeLoadedFromNativeZarStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeBindingMissingStatus(void);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsAdultLinkBinding(
    const Oot3dTitleIntroOpeningActorBindingRow* binding
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsEponaBinding(
    const Oot3dTitleIntroOpeningActorBindingRow* binding
);
const char* Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameNotSampledStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameMotionInactiveStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeLinkVisualTransformUnresolvedStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeLinkBindingMismatchStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeLinkVisualTransformStatus(u8 active);
const char* Oot3d_TitleIntroOpeningActorRuntimePairedMountVisualTransformUnresolvedStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimePairedMountBindingMismatchStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimePairedMountVisualTransformStatus(u16 sourceKind, u8 active);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsMountedGallopRouteDecoded(
    const Oot3dTitleIntroHorseStateRouteRow* route
);
u8 Oot3d_TitleIntroOpeningActorRuntimeCanLoadMountedGallopCarrot(
    const Oot3dTitleIntroOpeningActorBindingRow* binding,
    const Oot3dTitleIntroHorseStateRouteRow* route
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsDirectRecordLayoutDecoded(void);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsMotionConsumerSplitDecoded(void);
u8 Oot3d_TitleIntroOpeningActorRuntimeAreTitleActorScalesDecoded(
    const Oot3dTitleIntroActorScaleRow* linkRow,
    const Oot3dTitleIntroActorScaleRow* eponaRow
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsTitleActorAnimationRowDecoded(
    const Oot3dTitleIntroActorAnimationRow* row
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsEponaMotionAnimationRouteDecoded(
    const Oot3dTitleIntroActorMotionAnimationRow* row,
    const char* activeCsabName
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsHorseStateRouteTableDecoded(void);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsAdultLinkPlayerActionTableDecoded(void);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsLinkChildStateRouteTableDecoded(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMotionRoleForSpeed(
    const Oot3dTitleIntroActorMotionAnimationRow* row,
    float nativeSpeed
);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedGallopClipRole(
    const Oot3dTitleIntroHorseStateRouteRow* route,
    float nativeSpeed,
    float* outNativePlaySpeed
);
u8 Oot3d_TitleIntroOpeningActorRuntimeIsMountedGallopCarrotClipRole(const char* clipRole);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedGallopMotionRole(const char* clipRole);
u16 Oot3d_TitleIntroOpeningActorRuntimeMountedGallopAnimationIndex(
    const Oot3dTitleIntroHorseStateRouteRow* route,
    const char* clipRole
);
u8 Oot3d_TitleIntroOpeningActorRuntimeSampleMountedPlayerAnimationFrame(
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    float* outFrame
);
u8 Oot3d_TitleIntroOpeningActorRuntimeSelectClip(
    const Oot3dTitleIntroOpeningActorBindingRow* binding,
    const Oot3dTitleIntroActorMotionAnimationRow* motionAnimationRow,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    u8 usePairedMountMotion,
    const char* activeCsabName,
    Oot3dTitleIntroOpeningActorClipRuntimeSelection* outSelection
);
u8 Oot3d_TitleIntroOpeningActorRuntimeUsesNativeBgCheckGrounding(
    const Oot3dTitleIntroOpeningActorBindingRow* binding,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion
);
const char* Oot3d_TitleIntroOpeningActorRuntimeBgCheckGroundingSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeBgCheckGroundingNoFloorSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeBgCheckGroundingAppliedSource(void);
u8 Oot3d_TitleIntroOpeningActorRuntimePairedMountDrawOwnsRider(
    const Oot3dTitleIntroOpeningActorBindingRow* mountBinding,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    u8 mountVisualSubmitted
);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedLinkContextSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedPlayerShapeRotationSource(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedLinkDrawNotProcessedStatus(void);
const char* Oot3d_TitleIntroOpeningActorRuntimeMountedLinkDrawVisualStatus(u8 submitted);
Oot3dTitleIntroOpeningActorSampleStatus Oot3d_TitleIntroOpeningActorRuntimeSamplePlayerMotion(
    s32 frame,
    Oot3dTitleIntroOpeningActorMotionSample* outSample
);
void Oot3d_TitleIntroOpeningActorRuntimeInitPairedMountMotion(
    Oot3dTitleIntroOpeningPairedMountMotionState* state
);
u8 Oot3d_TitleIntroOpeningActorRuntimeStepPairedMountMotion(
    Oot3dTitleIntroOpeningPairedMountMotionState* state,
    Oot3dTitleIntroOpeningActorMotionSample* sample
);
u8 Oot3d_TitleIntroOpeningActorRuntimeSampleQdbCue(
    u16 qdbIndex,
    u32 commandId,
    float frame,
    Oot3dTitleIntroOpeningActorCueSample* outSample
);

#endif
