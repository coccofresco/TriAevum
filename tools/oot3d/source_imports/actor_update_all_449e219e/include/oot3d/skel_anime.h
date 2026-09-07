#ifndef OOT3D_SKEL_ANIME_H
#define OOT3D_SKEL_ANIME_H

#include "oot3d/types.h"

typedef struct Oot3dPlayState Oot3dPlayState;

enum {
    OOT3D_SKEL_ANIME_SIZE = 0x84,
    OOT3D_SKEL_ANIME_PLAY_TASK_QUEUE_OFFSET = 0x3410,
    OOT3D_GAME_STATE_UPDATE_RATE_OFFSET = 0x110,
    OOT3D_ANIMATION_TASK_SIZE = 0x20,
    OOT3D_ANIMATION_TASK_STORAGE_COUNT = 50,
    OOT3D_ANIMATION_TASK_MAX_ACTIVE = 49,
    OOT3D_JOINT_POSE_SIZE = 0x34,
    OOT3D_ANIMATION_SAMPLE_TRANSFORM_SIZE = 0x24,
    OOT3D_ANIMATION_SAMPLE_SCRATCH_COUNT = 64,
    OOT3D_DIRECT_ANIMATION_SAMPLE_SCRATCH_COUNT = 128,
    OOT3D_TRIG_SAMPLE_COUNT = 256,
};

typedef enum Oot3dAnimationMode {
    OOT3D_ANIMMODE_LOOP = 0,
    OOT3D_ANIMMODE_LOOP_INTERP = 1,
    OOT3D_ANIMMODE_ONCE = 2,
    OOT3D_ANIMMODE_ONCE_INTERP = 3,
    OOT3D_ANIMMODE_LOOP_PARTIAL = 4,
    OOT3D_ANIMMODE_LOOP_PARTIAL_INTERP = 5,
} Oot3dAnimationMode;

typedef enum Oot3dSkelAnimeUpdateMode {
    OOT3D_SKEL_UPDATE_NONE = 0,
    OOT3D_SKEL_UPDATE_QUEUED_LOOP = 1,
    OOT3D_SKEL_UPDATE_QUEUED_ONCE = 2,
    OOT3D_SKEL_UPDATE_QUEUED_MORPH = 3,
    OOT3D_SKEL_UPDATE_DIRECT_LOOP = 4,
    OOT3D_SKEL_UPDATE_DIRECT_PARTIAL_LOOP = 5,
    OOT3D_SKEL_UPDATE_DIRECT_ONCE = 6,
    OOT3D_SKEL_UPDATE_DIRECT_MORPH = 7,
    OOT3D_SKEL_UPDATE_DIRECT_MORPH_TAPERED = 8,
} Oot3dSkelAnimeUpdateMode;

typedef enum Oot3dAnimationTaper {
    OOT3D_ANIMTAPER_DECEL = -1,
    OOT3D_ANIMTAPER_NONE = 0,
    OOT3D_ANIMTAPER_ACCEL = 1,
} Oot3dAnimationTaper;

typedef struct Oot3dSkelAnimeDrawData {
    u32 unk_00;
    u32 userData;
    u32 unk_08;
    u32 owner;
    u32 overrideLimbDraw;
    u32 postLimbDraw;
} Oot3dSkelAnimeDrawData;

/*
 * Native 32-bit layout recovered from 0x0036B4EC and its animation-change
 * callers. Target pointers remain u32 so host-side validation does not alter
 * the 3DS field offsets.
 */
typedef struct Oot3dSkelAnime {
    u8 unk_00[0x04];
    u32 resourceArchive;
    Oot3dSkelAnimeDrawData drawDataStorage;
    u32 drawData;
    u8 unk_24[0x04];
    u32 skeletonResource;
    u8 unk_2C[0x04];
    s32 animationIndex;
    float morphWeight;
    float morphRate;
    float currentFrame;
    float playSpeed;
    float startFrame;
    float endFrame;
    float animationLength;
    s8 taper;
    u8 unk_51[0x1F];
    u8 animationMode;
    u8 updateMode;
    u8 unk_72[0x02];
    u8 limbCount;
    u8 specialLimbIndex;
    u8 poseFormat;
    u8 countedAllocation;
    u32 jointTable;
    u32 morphTable;
    u8 animationFormat;
    u8 queuedPoseTask;
    u8 ownsJointAndMorphTables;
    u8 unk_83;
} Oot3dSkelAnime;

typedef struct Oot3dAnimationTask {
    u32 queueContext;
    u32 owner;
    u8 contextTag;
    u8 limbCount;
    u8 unk_0A[0x02];
    u32 jointTable;
    u32 morphTable;
    float blendWeight;
    u8 unk_18[0x04];
    u8 type;
    u8 unk_1D[0x03];
} Oot3dAnimationTask;

typedef struct Oot3dAnimationTaskQueue {
    u32 context;
    u32 count;
    Oot3dAnimationTask tasks[OOT3D_ANIMATION_TASK_STORAGE_COUNT];
} Oot3dAnimationTaskQueue;

/*
 * Native pose matrices use twelve row-major floats with a 0x34-byte stride.
 * The trailing word is not touched by the sampling/interpolation routines.
 */
typedef struct Oot3dJointPose {
    float matrix[12];
    u32 unk_30;
} Oot3dJointPose;

typedef struct Oot3dAnimationSampleTransform {
    float translation[3];
    float rotation[3];
    float scale[3];
} Oot3dAnimationSampleTransform;

/*
 * Native CMB skeleton view used by 0x0048BA98. The pointer-bearing wrappers
 * intentionally use C pointers: they retain the observed 32-bit offsets on
 * 3DS while also allowing the same semantic code to be exercised on a
 * 64-bit validation host.
 */
typedef struct Oot3dSkeletonHeader {
    u32 unk_00;
    u32 unk_04;
    s32 limbCount;
} Oot3dSkeletonHeader;

typedef struct Oot3dSkeletonLimbTransform {
    u32 unk_00;
    float scale[3];
    float rotation[3];
    float translation[3];
} Oot3dSkeletonLimbTransform;

typedef struct Oot3dSkeletonAssetView {
    const Oot3dSkeletonHeader* header;
    const Oot3dSkeletonLimbTransform* limbTransforms;
} Oot3dSkeletonAssetView;

typedef struct Oot3dSkeletonResourceData {
    u32 unk_00;
    const Oot3dSkeletonAssetView* asset;
} Oot3dSkeletonResourceData;

typedef struct Oot3dSkeletonResource {
    u32 unk_00;
    const Oot3dSkeletonResourceData* data;
} Oot3dSkeletonResource;

/* The first word of the native animation resource points at its CSAB bytes. */
typedef struct Oot3dAnimationResource {
    const u8* data;
} Oot3dAnimationResource;

typedef struct Oot3dAnimationCurveState {
    const void* curve;
    u8 loop;
} Oot3dAnimationCurveState;

typedef struct Oot3dAnimationCurveHeader {
    u8 interpolationType;
    u8 unk_01[3];
    s32 keyCount;
    u32 unk_08;
    s32 duration;
} Oot3dAnimationCurveHeader;

typedef struct Oot3dAnimationCurveF32Key {
    s32 frame;
    float value;
    float tangentIn;
    float tangentOut;
} Oot3dAnimationCurveF32Key;

typedef struct Oot3dAnimationCurveF32LinearKey {
    s32 frame;
    float value;
} Oot3dAnimationCurveF32LinearKey;

typedef struct Oot3dAnimationCurveS16Key {
    s16 frame;
    s16 value;
    s16 tangentIn;
    s16 tangentOut;
} Oot3dAnimationCurveS16Key;

typedef struct Oot3dTrigSample {
    float sinValue;
    float cosValue;
    float sinDelta;
    float cosDelta;
} Oot3dTrigSample;

#if defined(__cplusplus)
static_assert(sizeof(Oot3dSkelAnime) == OOT3D_SKEL_ANIME_SIZE, "SkelAnime size");
static_assert(sizeof(Oot3dSkelAnimeDrawData) == 0x18, "SkelAnime draw-data size");
static_assert(offsetof(Oot3dSkelAnime, drawDataStorage) == 0x08, "SkelAnime draw-data storage");
static_assert(offsetof(Oot3dSkelAnime, drawData) == 0x20, "SkelAnime draw-data pointer");
static_assert(offsetof(Oot3dSkelAnime, skeletonResource) == 0x28, "SkelAnime model offset");
static_assert(offsetof(Oot3dSkelAnime, animationIndex) == 0x30, "SkelAnime animationIndex offset");
static_assert(offsetof(Oot3dSkelAnime, morphWeight) == 0x34, "SkelAnime morphWeight offset");
static_assert(offsetof(Oot3dSkelAnime, currentFrame) == 0x3C, "SkelAnime currentFrame offset");
static_assert(offsetof(Oot3dSkelAnime, animationMode) == 0x70, "SkelAnime animationMode offset");
static_assert(offsetof(Oot3dSkelAnime, updateMode) == 0x71, "SkelAnime updateMode offset");
static_assert(offsetof(Oot3dSkelAnime, jointTable) == 0x78, "SkelAnime jointTable offset");
static_assert(sizeof(Oot3dAnimationTask) == OOT3D_ANIMATION_TASK_SIZE, "animation task size");
static_assert(offsetof(Oot3dAnimationTask, contextTag) == 0x08, "animation task context offset");
static_assert(offsetof(Oot3dAnimationTask, jointTable) == 0x0C, "animation task joint offset");
static_assert(offsetof(Oot3dAnimationTask, blendWeight) == 0x14, "animation task blend offset");
static_assert(offsetof(Oot3dAnimationTask, type) == 0x1C, "animation task type offset");
static_assert(sizeof(Oot3dJointPose) == OOT3D_JOINT_POSE_SIZE, "joint pose size");
static_assert(sizeof(Oot3dAnimationSampleTransform) == OOT3D_ANIMATION_SAMPLE_TRANSFORM_SIZE,
              "animation sample transform size");
static_assert(sizeof(Oot3dSkeletonHeader) == 0x0C, "skeleton header size");
static_assert(sizeof(Oot3dSkeletonLimbTransform) == 0x28, "skeleton limb transform size");
static_assert(sizeof(Oot3dAnimationCurveHeader) == 0x10, "animation curve header size");
static_assert(sizeof(Oot3dAnimationCurveF32Key) == 0x10, "f32 curve key size");
static_assert(sizeof(Oot3dAnimationCurveF32LinearKey) == 0x08, "linear curve key size");
static_assert(sizeof(Oot3dAnimationCurveS16Key) == 0x08, "s16 curve key size");
static_assert(sizeof(Oot3dTrigSample) == 0x10, "trig sample size");
#else
_Static_assert(sizeof(Oot3dSkelAnime) == OOT3D_SKEL_ANIME_SIZE, "SkelAnime size");
_Static_assert(sizeof(Oot3dSkelAnimeDrawData) == 0x18, "SkelAnime draw-data size");
_Static_assert(offsetof(Oot3dSkelAnime, drawDataStorage) == 0x08, "SkelAnime draw-data storage");
_Static_assert(offsetof(Oot3dSkelAnime, drawData) == 0x20, "SkelAnime draw-data pointer");
_Static_assert(offsetof(Oot3dSkelAnime, skeletonResource) == 0x28, "SkelAnime model offset");
_Static_assert(offsetof(Oot3dSkelAnime, animationIndex) == 0x30, "SkelAnime animationIndex offset");
_Static_assert(offsetof(Oot3dSkelAnime, morphWeight) == 0x34, "SkelAnime morphWeight offset");
_Static_assert(offsetof(Oot3dSkelAnime, currentFrame) == 0x3C, "SkelAnime currentFrame offset");
_Static_assert(offsetof(Oot3dSkelAnime, animationMode) == 0x70, "SkelAnime animationMode offset");
_Static_assert(offsetof(Oot3dSkelAnime, updateMode) == 0x71, "SkelAnime updateMode offset");
_Static_assert(offsetof(Oot3dSkelAnime, jointTable) == 0x78, "SkelAnime jointTable offset");
_Static_assert(sizeof(Oot3dAnimationTask) == OOT3D_ANIMATION_TASK_SIZE, "animation task size");
_Static_assert(offsetof(Oot3dAnimationTask, contextTag) == 0x08, "animation task context offset");
_Static_assert(offsetof(Oot3dAnimationTask, jointTable) == 0x0C, "animation task joint offset");
_Static_assert(offsetof(Oot3dAnimationTask, blendWeight) == 0x14, "animation task blend offset");
_Static_assert(offsetof(Oot3dAnimationTask, type) == 0x1C, "animation task type offset");
_Static_assert(sizeof(Oot3dJointPose) == OOT3D_JOINT_POSE_SIZE, "joint pose size");
_Static_assert(sizeof(Oot3dAnimationSampleTransform) == OOT3D_ANIMATION_SAMPLE_TRANSFORM_SIZE,
               "animation sample transform size");
_Static_assert(sizeof(Oot3dSkeletonHeader) == 0x0C, "skeleton header size");
_Static_assert(sizeof(Oot3dSkeletonLimbTransform) == 0x28, "skeleton limb transform size");
_Static_assert(sizeof(Oot3dAnimationCurveHeader) == 0x10, "animation curve header size");
_Static_assert(sizeof(Oot3dAnimationCurveF32Key) == 0x10, "f32 curve key size");
_Static_assert(sizeof(Oot3dAnimationCurveF32LinearKey) == 0x08, "linear curve key size");
_Static_assert(sizeof(Oot3dAnimationCurveS16Key) == 0x08, "s16 curve key size");
_Static_assert(sizeof(Oot3dTrigSample) == 0x10, "trig sample size");
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* DAT_0051B2F4: current native GameState pointer. */
extern u8* gOot3dGameState;
/* DAT_0054960C: animation-task runtime state; word 2 supplies a task tag. */
extern u32 gOot3dAnimationTaskState[];
/* DAT_005A54D8: shared 64-entry transform scratch used while sampling CSABs. */
extern Oot3dAnimationSampleTransform
    gOot3dAnimationSampleScratch[OOT3D_ANIMATION_SAMPLE_SCRATCH_COUNT];
/* DAT_005642D0: 128-entry scratch used by SkelAnime_GetFrameData. */
extern Oot3dAnimationSampleTransform
    gOot3dDirectAnimationSampleScratch[
        OOT3D_DIRECT_ANIMATION_SAMPLE_SCRATCH_COUNT
    ];
/* DAT_004DF42C: 256 sin/cos samples plus linear deltas. */
extern const Oot3dTrigSample gOot3dTrigTable[OOT3D_TRIG_SAMPLE_COUNT];

#if defined(OOT3D_HOST_SKEL_FREE_PTR32_RESOLVER)
void* oot3d_host_skel_free_ptr32_resolve(u32 address);
#endif

void SystemArena_Free(void* pointer);
void SkelAnime_Free2(Oot3dSkelAnime* skelAnime);

/*
 * Typed contracts for the native helpers called by SkelAnime_Update. The
 * trailing float arguments use s0 under the 3DS hard-float ABI.
 */
void SkelAnime_GetFrameData(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    u8 limbCount,
    u32 jointTable,
    float frame
);
void Oot3d_SkelAnimeSampleDirect(Oot3dSkelAnime* skelAnime);
void Oot3d_SkelAnimeSampleQueued(Oot3dSkelAnime* skelAnime, Oot3dPlayState* play);
Oot3dAnimationTask* Oot3d_AnimationTaskQueuePush(
    Oot3dAnimationTaskQueue* queue,
    Oot3dSkelAnime* skelAnime
);
void Oot3d_AnimationTaskSetBlend(
    Oot3dAnimationTask* task,
    u8 limbCount,
    u32 jointTable,
    u32 morphTable,
    float blendWeight
);
uintptr_t ZAR_GetCSABByIndex(u32 resourceArchive, s32 animationIndex);
void Oot3d_AnimationTaskSetSample(
    Oot3dAnimationTask* task,
    u32 skeletonResource,
    u32 animationResource,
    u8 limbCount,
    Oot3dJointPose* jointTable,
    u8 queuedPoseTask,
    u32 unk,
    float frame
);
void Oot3d_AnimationTaskSamplePose(
    Oot3dAnimationTask* task,
    u8 specialLimbIndex,
    u32 skeletonResource,
    u32 animationResource,
    u8 limbCount,
    Oot3dJointPose* jointTable,
    u8 roundFrame,
    float frame
);
void Oot3d_CopySkeletonLimbTransform(
    const Oot3dSkeletonLimbTransform* const* source,
    float translation[3],
    float rotation[3],
    float scale[3]
);
float Oot3d_EvalAnimationCurveS16(
    const Oot3dAnimationCurveState* state,
    float frame
);
float Oot3d_EvalAnimationCurveF32(
    const Oot3dAnimationCurveState* state,
    float frame
);
void Oot3d_InitSkeletonPoseTransforms(
    const Oot3dSkeletonResource* skeletonResource,
    Oot3dAnimationSampleTransform* transforms
);
void Oot3d_SampleAnimationLimbTransform(
    const Oot3dAnimationResource* animationResource,
    u32 limbIndex,
    Oot3dAnimationSampleTransform* transform,
    u32 channelMask,
    float frame
);
void Oot3d_SampleAnimationNodeTransforms(
    const Oot3dAnimationResource* animationResource,
    Oot3dAnimationSampleTransform* transforms,
    float frame
);
void Oot3d_SampleAnimationLimbQuaternion(
    const Oot3dAnimationResource* animationResource,
    u32 limbIndex,
    Oot3dAnimationSampleTransform* transform,
    float quaternion[4],
    u32 channelMask,
    float frame
);
void Oot3d_GetFrameDataFromResources(
    const Oot3dSkeletonResource* skeletonResource,
    const Oot3dAnimationResource* animationResource,
    u8 animationFormat,
    u8 poseFormat,
    u8 specialLimbIndex,
    u8 roundFrame,
    u32 limbCount,
    Oot3dJointPose* jointTable,
    float frame
);
void Oot3d_ChangeAnimationFromResource(
    Oot3dSkelAnime* skelAnime,
    Oot3dAnimationTaskQueue* queue,
    const Oot3dAnimationResource* animationResource,
    Oot3dJointPose* jointTable,
    Oot3dJointPose* morphTable,
    s32 animationIndex,
    u8 animationMode,
    float playSpeed,
    float startFrame,
    float endFrame,
    float morphFrames
);
void Oot3d_ChangeAnimationByIndex(
    Oot3dSkelAnime* skelAnime,
    Oot3dPlayState* play,
    s32 animationIndex,
    u8 animationMode,
    float playSpeed,
    float startFrame,
    float endFrame,
    float morphFrames
);
void Oot3d_AnimationChangeImpl(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float playSpeed,
    float startFrame,
    float endFrame,
    u8 animationMode,
    float morphFrames,
    s8 taper
);
void Oot3d_AnimationChangeNoTaper(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float playSpeed,
    float startFrame,
    float endFrame,
    u8 animationMode,
    float morphFrames
);
void SkelAnime_SetUpdate(Oot3dSkelAnime* skelAnime);
void SkelAnime_CopyFrameTable(
    const Oot3dSkelAnime* skelAnime,
    Oot3dJointPose* destination,
    const Oot3dJointPose* source
);
s32 Animation_GetLength(
    const Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
s32 Oot3d_GetAnimationLastFrame(
    const Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
s32 Oot3d_GetAnimationLastFrameS16(
    const Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
s32 Animation_GetLastFrame(
    const Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
void Oot3d_PlayAnimationOnce(
    Oot3dSkelAnime* skelAnime,
    Oot3dPlayState* play,
    s32 animationIndex
);
void Oot3d_InterpJointPoseAlternate(
    u32 interpolateTranslation,
    Oot3dJointPose* destination,
    const Oot3dJointPose* basePose,
    const Oot3dJointPose* morphPose,
    float blendWeight
);
void Oot3d_InterpJointPoses(
    u8 limbCount,
    u8 specialLimbIndex,
    Oot3dJointPose* destination,
    const Oot3dJointPose* basePose,
    const Oot3dJointPose* morphPose,
    float blendWeight
);
void Oot3d_InterpJointPosesAlternate(
    u8 limbCount,
    u8 specialLimbIndex,
    Oot3dJointPose* destination,
    const Oot3dJointPose* basePose,
    const Oot3dJointPose* morphPose,
    float blendWeight
);

/*
 * 0x0036B4EC is the full native animation update state machine. Its second
 * argument supplies the optional PlayState-dependent update context.
 */
s32 SkelAnime_Update(Oot3dSkelAnime* skelAnime, Oot3dPlayState* play);

/* 0x003731E0: the high-fan-in wrapper used by actors without a PlayState. */
s32 SkelAnime_UpdateNoPlayState(Oot3dSkelAnime* skelAnime);
s32 SkelAnime_UpdateNoPlayState_00370734(
    Oot3dSkelAnime* skelAnime
);
void SkelAnime_SetPlaySpeed(
    Oot3dSkelAnime* skelAnime,
    float playSpeed
);
s32 Animation_OnFrameImpl(
    const Oot3dSkelAnime* skelAnime,
    float frame,
    float updateRate
);
void Animation_PlayLoop(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
void Animation_PlayLoopSetSpeed(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float playSpeed
);
void Animation_MorphToLoop(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float morphFrames
);
void Animation_PlayOnce(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex
);
void Animation_PlayOnceSetSpeed(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float playSpeed
);
void Animation_MorphToPlayOnce(
    Oot3dSkelAnime* skelAnime,
    s32 animationIndex,
    float morphFrames
);

#ifdef __cplusplus
}
#endif

static inline void* oot3d_skel_free_ptr32_to_host(u32 address) {
#if defined(OOT3D_HOST_SKEL_FREE_PTR32_RESOLVER)
    return oot3d_host_skel_free_ptr32_resolve(address);
#else
    return (void*)(uintptr_t)address;
#endif
}

#endif
