#ifndef OOT3D_ACTOR_BGCHECK_H
#define OOT3D_ACTOR_BGCHECK_H

#include "oot3d/actor.h"

typedef struct Oot3dPlayState Oot3dPlayState;

typedef struct Oot3dCollisionPoly {
    u16 type;
    u8 unk_02[0x08];
    s16 normalX;
    s16 normalY;
    s16 normalZ;
} Oot3dCollisionPoly;

typedef struct Oot3dBgCheckVec3i {
    s32 x;
    s32 y;
    s32 z;
} Oot3dBgCheckVec3i;

typedef struct Oot3dDynaPolyActor {
    Oot3dActor actor;
    s32 bgId;
    float unk_1A8;
    float unk_1AC;
    u8 unk_1B0[0x04];
    u32 transformFlags;
    u8 interactFlags;
    u8 unk_1B9[0x03];
} Oot3dDynaPolyActor;

#if defined(__cplusplus)
static_assert(
    offsetof(Oot3dDynaPolyActor, bgId) == 0x1A4,
    "DynaPolyActor bgId"
);
static_assert(
    offsetof(Oot3dDynaPolyActor, transformFlags) == 0x1B4,
    "DynaPolyActor transform flags"
);
#else
_Static_assert(
    offsetof(Oot3dDynaPolyActor, bgId) == 0x1A4,
    "DynaPolyActor bgId"
);
_Static_assert(
    offsetof(Oot3dDynaPolyActor, transformFlags) == 0x1B4,
    "DynaPolyActor transform flags"
);
#endif

enum {
    OOT3D_ACTOR_BGCHECK_SCENE = 0x32,
    OOT3D_BGCHECK_GROUND = 0x0001,
    OOT3D_BGCHECK_GROUND_TOUCH = 0x0002,
    OOT3D_BGCHECK_GROUND_LEAVE = 0x0004,
    OOT3D_BGCHECK_WALL = 0x0008,
    OOT3D_BGCHECK_CEILING = 0x0010,
    OOT3D_BGCHECK_WATER = 0x0020,
    OOT3D_BGCHECK_WATER_TOUCH = 0x0040,
    OOT3D_BGCHECK_FLOOR_BELOW = 0x0080,
    OOT3D_BGCHECK_CRUSHED = 0x0100,
};

enum {
    OOT3D_UPDBGCHECK_WALL = 0x01,
    OOT3D_UPDBGCHECK_CEILING = 0x02,
    OOT3D_UPDBGCHECK_FLOOR_WATER = 0x04,
    OOT3D_UPDBGCHECK_FLOOR_GRAVITY = 0x08,
    OOT3D_UPDBGCHECK_RESET_VELOCITY = 0x10,
    OOT3D_UPDBGCHECK_NO_RIPPLES = 0x40,
    OOT3D_UPDBGCHECK_WALL_ALT = 0x80,
};

#define OOT3D_BGCHECK_Y_MIN (-32000.0f)

#ifdef __cplusplus
extern "C" {
#endif

extern u32 gOot3dCurrentCeilingPoly;
extern s32 gOot3dCurrentCeilingBgId;

#if defined(OOT3D_HOST_PTR32_RESOLVER)
void* oot3d_host_ptr32_resolve(u32 address);
#endif

s32 DynaPoly_TransformCarriedActor(
    void* colCtx,
    s32 bgId,
    Oot3dActor* actor
);
void DynaPoly_SetActorOnTop(
    void* colCtx,
    Oot3dActor* actor,
    s32 bgId
);
s32 BgCheck_EntitySphVsWall3(
    void* colCtx,
    Oot3dActorVec3f* result,
    const Oot3dActorVec3f* current,
    const Oot3dActorVec3f* previous,
    float radius,
    u32* outPoly,
    s32* outBgId,
    Oot3dActor* actor,
    float height
);
s32 BgCheck_EntitySphVsWall1(
    void* colCtx,
    Oot3dActorVec3f* result,
    const Oot3dActorVec3f* current,
    const Oot3dActorVec3f* previous,
    float radius,
    u32* outPoly,
    float height
);
s32 BgCheck_EntitySphVsWall4(
    void* colCtx,
    Oot3dActorVec3f* result,
    const Oot3dActorVec3f* current,
    const Oot3dActorVec3f* previous,
    float radius,
    u32* outPoly,
    s32* outBgId,
    Oot3dActor* actor,
    float height
);
s32 BgCheck_EntityCheckCeiling(
    void* colCtx,
    float* outY,
    Oot3dActorVec3f* position,
    float distance,
    u32* outPoly,
    s32* outBgId,
    Oot3dActor* actor
);
void BgCheck_GetStaticLookupIndicesFromPos(
    void* colCtx,
    const Oot3dActorVec3f* position,
    Oot3dBgCheckVec3i* sector
);
s32 BgCheck_EntityLineTest1(
    void* colCtx,
    const Oot3dActorVec3f* posA,
    const Oot3dActorVec3f* posB,
    Oot3dActorVec3f* result,
    u32* outPoly,
    s32 checkWall,
    s32 checkFloor,
    s32 checkCeiling,
    s32 checkOneFace,
    s32* outBgId
);
float BgCheck_EntityRaycastFloor5(
    Oot3dPlayState* play,
    void* colCtx,
    u32* outPoly,
    s32* outBgId,
    Oot3dActor* actor,
    Oot3dActorVec3f* position
);
float BgCheck_EntityRaycastFloor3(
    void* colCtx,
    u32* outPoly,
    s32* outBgId,
    Oot3dActorVec3f* position
);
float BgCheck_EntityRaycastFloor4(
    void* colCtx,
    u32* outPoly,
    s32* outBgId,
    Oot3dActor* actor,
    Oot3dActorVec3f* position
);
u32 oot3d_surface_type_get_word(
    void* colCtx,
    const Oot3dCollisionPoly* poly,
    u32 bgId,
    s32 dataIndex
);
u16 oot3d_surface_type_get_sfx_semantic(
    void* colCtx,
    const Oot3dCollisionPoly* poly,
    u32 bgId
);
u32 SurfaceType_GetFloorType(
    void* colCtx,
    const Oot3dCollisionPoly* poly,
    u32 bgId
);
s32 FUN_002c1e10(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
u32 FUN_00314aa0(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
s32 FUN_003232a4(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
s32 FUN_00331030(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
s16 FUN_00357d54(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
u8 FUN_0035ea4c(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
s8 FUN_00496914(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
u32 FUN_0049f97c(
    void* collisionContext,
    const Oot3dCollisionPoly* poly,
    u8 bgId
);
u32 SurfaceType_IsHorseBlocked(
    void* colCtx,
    const Oot3dCollisionPoly* poly,
    u32 bgId
);
Oot3dDynaPolyActor* DynaPoly_GetActor(
    void* colCtx,
    s32 bgId
);
s32 WaterBox_GetSurface1(
    Oot3dPlayState* play,
    void* colCtx,
    float x,
    float z,
    float* outY,
    u32* outWaterBox
);
void EffectSsGRipple_Spawn(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position,
    s16 radius,
    s16 radiusMax,
    s16 life
);
s16 Math_Atan2S(float y, float x);

void Actor_UpdateBgCheckInfo(
    Oot3dPlayState* play,
    Oot3dActor* actor,
    float wallCheckHeight,
    float wallCheckRadius,
    float ceilingCheckHeight,
    u32 flags
);

#ifdef __cplusplus
}
#endif

static inline void* oot3d_ptr32_to_host(u32 address) {
#if defined(OOT3D_HOST_PTR32_RESOLVER)
    return oot3d_host_ptr32_resolve(address);
#else
    return (void*)(uintptr_t)address;
#endif
}

#endif
