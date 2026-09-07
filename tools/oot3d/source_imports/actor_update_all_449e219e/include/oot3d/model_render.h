#ifndef OOT3D_MODEL_RENDER_H
#define OOT3D_MODEL_RENDER_H

#include "oot3d/static_init.h"
#include "oot3d/types.h"

typedef struct Oot3dSkeletonAnimationModel
    Oot3dSkeletonAnimationModel;

typedef struct Oot3dSkeletonAnimationModelVtable {
    void (*destroy)(Oot3dSkeletonAnimationModel* model);
    void (*deleteObject)(Oot3dSkeletonAnimationModel* model);
    void (*prepareRender)(Oot3dSkeletonAnimationModel* model);
} Oot3dSkeletonAnimationModelVtable;

/* Native 0xB0-byte skeleton model prefix used by both submission paths. */
struct Oot3dSkeletonAnimationModel {
    Oot3dSkeletonAnimationModelVtable* vtable;
#if UINTPTR_MAX == UINT32_MAX
    u8 unk_04[0x78];
#else
    u8 unk_08[0x74];
#endif
    float rootTransform[12];
    u8 poseReady;
    u8 unk_0AD[0x03];
};

typedef struct Oot3dModelRenderQueueEntry {
    Oot3dSkeletonAnimationModel* model;
    u8 state;
    u8 unk_05[3];
} Oot3dModelRenderQueueEntry;

typedef struct Oot3dModelRenderQueue {
    u32 unk_00;
    u32 forcePrimary;
    u32 unk_08;
    u32 secondaryCount;
    u32 unk_10;
    u32 capacity;
    u32* primaryCount;
    Oot3dModelRenderQueueEntry* primaryEntries;
    u32 unk_20;
    Oot3dModelRenderQueueEntry* secondaryEntries;
} Oot3dModelRenderQueue;

#if UINTPTR_MAX == UINT32_MAX
#if defined(__cplusplus)
static_assert(
    sizeof(Oot3dSkeletonAnimationModelVtable) == 0x0C,
    "model vtable size"
);
static_assert(
    sizeof(Oot3dModelRenderQueueEntry) == 0x08,
    "model queue entry size"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, forcePrimary) == 0x04,
    "model queue force-primary offset"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, secondaryCount) == 0x0C,
    "model queue secondary-count offset"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, capacity) == 0x14,
    "model queue capacity offset"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, primaryCount) == 0x18,
    "model queue primary-count offset"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, primaryEntries) == 0x1C,
    "model queue primary-entry offset"
);
static_assert(
    offsetof(Oot3dModelRenderQueue, secondaryEntries) == 0x24,
    "model queue secondary-entry offset"
);
#else
_Static_assert(
    sizeof(Oot3dSkeletonAnimationModelVtable) == 0x0C,
    "model vtable size"
);
_Static_assert(
    sizeof(Oot3dModelRenderQueueEntry) == 0x08,
    "model queue entry size"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, forcePrimary) == 0x04,
    "model queue force-primary offset"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, secondaryCount) == 0x0C,
    "model queue secondary-count offset"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, capacity) == 0x14,
    "model queue capacity offset"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, primaryCount) == 0x18,
    "model queue primary-count offset"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, primaryEntries) == 0x1C,
    "model queue primary-entry offset"
);
_Static_assert(
    offsetof(Oot3dModelRenderQueue, secondaryEntries) == 0x24,
    "model queue secondary-entry offset"
);
#endif
#endif

#if defined(__cplusplus)
static_assert(
    offsetof(Oot3dSkeletonAnimationModel, rootTransform) == 0x7C,
    "skeleton model root-transform offset"
);
static_assert(
    offsetof(Oot3dSkeletonAnimationModel, poseReady) == 0xAC,
    "skeleton model pose-ready offset"
);
static_assert(
    sizeof(Oot3dSkeletonAnimationModel) == 0xB0,
    "skeleton model size"
);
#else
_Static_assert(
    offsetof(Oot3dSkeletonAnimationModel, rootTransform) == 0x7C,
    "skeleton model root-transform offset"
);
_Static_assert(
    offsetof(Oot3dSkeletonAnimationModel, poseReady) == 0xAC,
    "skeleton model pose-ready offset"
);
_Static_assert(
    sizeof(Oot3dSkeletonAnimationModel) == 0xB0,
    "skeleton model size"
);
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern Oot3dStaticInitGuard gOot3dRendererInitGuard;
extern u8 gOot3dRendererGlobalState[];
extern Oot3dModelRenderQueue gOot3dModelRenderQueue;

void RendererGlobalState_Init(void* state);

void ModelRenderQueueEntry_Init(
    Oot3dModelRenderQueue* queue,
    Oot3dModelRenderQueueEntry* entry,
    Oot3dSkeletonAnimationModel* model
);
void ModelRenderQueue_Submit(
    Oot3dModelRenderQueue* queue,
    Oot3dSkeletonAnimationModel* model,
    s32 mode
);
void ModelHandle_Submit(
    Oot3dSkeletonAnimationModel* model,
    s32 mode
);
void ModelMesh_HidePart(
    Oot3dSkeletonAnimationModel* model,
    s32 part
);
void ModelMesh_ShowPart(
    Oot3dSkeletonAnimationModel* model,
    s32 part
);

#ifdef __cplusplus
}
#endif

#endif
