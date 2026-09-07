#ifndef OOT3D_ACTOR_SPAWN_H
#define OOT3D_ACTOR_SPAWN_H

#include "oot3d/actor.h"

typedef struct Oot3dPlayState Oot3dPlayState;
typedef struct Oot3dActorAllocator Oot3dActorAllocator;

enum {
    OOT3D_ACTOR_CATEGORY_COUNT = 12,
    OOT3D_OBJECT_SLOT_COUNT = 19,
    OOT3D_ACTOR_CONTEXT_PLAY_OFFSET = 0x208C,
    OOT3D_OBJECT_CONTEXT_PLAY_OFFSET = 0x3A58,
    OOT3D_CURRENT_ROOM_PLAY_OFFSET = 0x4C30,
    OOT3D_SCENE_ACTOR_COUNT_PLAY_OFFSET = 0x5C03,
    OOT3D_SCENE_ACTOR_LIST_PLAY_OFFSET = 0x5C10,
    OOT3D_RENDERER_ACTOR_CONTEXT_OFFSET = 0x174,
    OOT3D_ACTOR_MODEL_CONTEXT_SIZE = 0x234,
};

typedef struct Oot3dActorListEntry {
    u32 count;
    u32 head;
} Oot3dActorListEntry;

typedef struct Oot3dActorContext {
    u8 unk_000[0x08];
    u8 total;
    u8 unk_009[0x03];
    Oot3dActorListEntry lists[OOT3D_ACTOR_CATEGORY_COUNT];
    u8 unk_06C[0x144];
    u32 roomClearFlags;
    u8 unk_1B4[0x58];
} Oot3dActorContext;

typedef struct Oot3dObjectStatus {
    u8 unk_00[0x04];
    s16 id;
    u8 unk_06[0x7A];
} Oot3dObjectStatus;

typedef union Oot3dObjectContext {
    struct {
        u8 count;
        u8 unk_001[0x7F];
    } header;
    Oot3dObjectStatus slots[OOT3D_OBJECT_SLOT_COUNT];
} Oot3dObjectContext;

typedef struct Oot3dActorProfile {
    s16 id;
    u8 category;
    u8 unk_03;
    u32 flags;
    s16 objectId;
    u8 unk_0A[0x02];
    u32 instanceSize;
    u32 init;
    u32 destroy;
    u32 update;
    u32 draw;
} Oot3dActorProfile;

typedef struct Oot3dActorOverlayEntry {
    u8 unk_00[0x10];
    u32 loadedRamAddr;
    u32 profile;
    u8 unk_18[0x04];
    u16 flags;
    u8 numLoaded;
    u8 unk_1F;
} Oot3dActorOverlayEntry;

typedef struct Oot3dSceneActorEntry {
    s16 id;
    s16 posX;
    s16 posY;
    s16 posZ;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    s16 params;
} Oot3dSceneActorEntry;

typedef struct Oot3dActorAllocatorVtable {
    u32 destroy;
    u32 deleteObject;
    u32 unk_08;
    u32 allocate;
} Oot3dActorAllocatorVtable;

struct Oot3dActorAllocator {
    u32 vtable;
};

typedef void (*Oot3dActorCallback)(
    Oot3dActor* actor,
    Oot3dPlayState* play
);
typedef void* (*Oot3dActorAllocatorAllocate)(
    Oot3dActorAllocator* allocator,
    u32 size,
    const char* sourceFile,
    u32 sourceLine
);

#if defined(__cplusplus)
static_assert(sizeof(Oot3dActorListEntry) == 0x08, "Actor list size");
static_assert(sizeof(Oot3dActorContext) == 0x20C, "Actor context size");
static_assert(
    offsetof(Oot3dActorContext, roomClearFlags) == 0x1B0,
    "Actor room-clear flags offset"
);
static_assert(sizeof(Oot3dObjectStatus) == 0x80, "Object status size");
static_assert(sizeof(Oot3dActorProfile) == 0x20, "Actor profile size");
static_assert(
    sizeof(Oot3dActorOverlayEntry) == 0x20,
    "Actor overlay-entry size"
);
static_assert(sizeof(Oot3dSceneActorEntry) == 0x10, "Scene actor size");
#else
_Static_assert(sizeof(Oot3dActorListEntry) == 0x08, "Actor list size");
_Static_assert(sizeof(Oot3dActorContext) == 0x20C, "Actor context size");
_Static_assert(
    offsetof(Oot3dActorContext, roomClearFlags) == 0x1B0,
    "Actor room-clear flags offset"
);
_Static_assert(sizeof(Oot3dObjectStatus) == 0x80, "Object status size");
_Static_assert(
    sizeof(Oot3dObjectContext) ==
        OOT3D_OBJECT_SLOT_COUNT * sizeof(Oot3dObjectStatus),
    "Object context slot coverage"
);
_Static_assert(sizeof(Oot3dActorProfile) == 0x20, "Actor profile size");
_Static_assert(
    sizeof(Oot3dActorOverlayEntry) == 0x20,
    "Actor overlay-entry size"
);
_Static_assert(sizeof(Oot3dSceneActorEntry) == 0x10, "Scene actor size");
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern Oot3dActorOverlayEntry gOot3dActorOverlayTable[];
extern u32 gOot3dActorAllocator;

#if defined(OOT3D_HOST_ACTOR_SPAWN_PTR32_RESOLVER)
void* oot3d_host_actor_spawn_ptr32_resolve(u32 address);
u32 oot3d_host_actor_spawn_ptr32_encode(const void* pointer);
Oot3dActorCallback oot3d_host_actor_spawn_callback_resolve(
    u32 address
);
Oot3dActorAllocatorAllocate
oot3d_host_actor_spawn_allocator_resolve(u32 address);
#endif

s32 Object_GetIndex(
    const Oot3dObjectContext* objectContext,
    s32 objectId
);
void CollisionCheck_InitInfo(Oot3dActorColChkInfo* info);
void Actor_Init(
    Oot3dActor* actor,
    Oot3dPlayState* play,
    s32 initializeNow
);
Oot3dActor* Actor_Spawn(
    Oot3dActorContext* actorContext,
    Oot3dPlayState* play,
    s32 actorId,
    float x,
    float y,
    float z,
    s16 rotX,
    s16 rotY,
    s16 rotZ,
    s16 params,
    s32 initializeNow
);
Oot3dActor* Actor_SpawnAsChild(
    Oot3dActorContext* actorContext,
    Oot3dActor* parent,
    Oot3dPlayState* play,
    s32 actorId,
    float x,
    float y,
    float z,
    s16 rotX,
    s16 rotY,
    s16 rotZ,
    s16 params
);
void Actor_SpawnSceneEntries(Oot3dPlayState* play);
s32 Actor_RunDeferredInit(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext
);

void* ZeldaArena_Malloc(size_t size);
void ZeldaArena_Free(void* pointer);
void* AutoClass1(void* allocation);

#ifdef __cplusplus
}
#endif

static inline void* oot3d_actor_spawn_ptr32_to_host(u32 address) {
#if defined(OOT3D_HOST_ACTOR_SPAWN_PTR32_RESOLVER)
    return oot3d_host_actor_spawn_ptr32_resolve(address);
#else
    return (void*)(uintptr_t)address;
#endif
}

static inline u32 oot3d_actor_spawn_ptr32_from_host(
    const void* pointer
) {
#if defined(OOT3D_HOST_ACTOR_SPAWN_PTR32_RESOLVER)
    return oot3d_host_actor_spawn_ptr32_encode(pointer);
#else
    return (u32)(uintptr_t)pointer;
#endif
}

#endif
