#ifndef OOT3D_COLLISION_CHECK_H
#define OOT3D_COLLISION_CHECK_H

#include <stdbool.h>

#include "oot3d/game_math.h"

typedef struct Oot3dPlayState Oot3dPlayState;

enum {
    OOT3D_COLLISION_AT_MAX = 50,
    OOT3D_COLLISION_AC_MAX = 60,
    OOT3D_COLLISION_OC_MAX = 50,
    OOT3D_COLLIDER_SHAPE_JNTSPH = 0,
    OOT3D_COLLIDER_SHAPE_CYLINDER = 1,
    OOT3D_COLLIDER_SHAPE_TRIS = 2,
    OOT3D_COLLIDER_SHAPE_QUAD = 3,
    OOT3D_COLLIDER_SHAPE_INVALID = 4,
    OOT3D_COLLIDER_TYPE_DEFAULT = 3,
    OOT3D_COLLIDER_OC2_TYPE_1 = 0x10,
};

/*
 * Native collision pointers remain 32-bit values so the maintained layouts
 * retain the offsets used by the ARM target on 64-bit validation hosts.
 */
typedef struct Oot3dCollider {
    u32 actor;
    u32 at;
    u32 ac;
    u32 oc;
    u8 atFlags;
    u8 acFlags;
    u8 ocFlags1;
    u8 ocFlags2;
    u8 colType;
    u8 shape;
    u8 pad_016[2];
} Oot3dCollider;

typedef struct Oot3dColliderInfo {
    u32 toucherDamageFlags;
    u8 toucherEffect;
    u8 toucherDamage;
    u8 pad_006[2];
    u32 bumperDamageFlags;
    u8 bumperEffect;
    u8 bumperDefense;
    s16 bumperHitPosX;
    s16 bumperHitPosY;
    s16 bumperHitPosZ;
    u8 elemType;
    u8 toucherFlags;
    u8 bumperFlags;
    u8 ocElemFlags;
    u32 atHit;
    u32 acHit;
    u32 atHitInfo;
    u32 acHitInfo;
} Oot3dColliderInfo;

typedef struct Oot3dColliderCylinderDim {
    float radius;
    float height;
    float yShift;
    Oot3dActorVec3f position;
} Oot3dColliderCylinderDim;

typedef struct Oot3dColliderCylinder {
    Oot3dCollider base;
    Oot3dColliderInfo info;
    Oot3dColliderCylinderDim dim;
} Oot3dColliderCylinder;

typedef struct Oot3dColliderInit {
    u8 colType;
    u8 atFlags;
    u8 acFlags;
    u8 ocFlags1;
    u8 ocFlags2;
    u8 shape;
    u8 pad_006[2];
} Oot3dColliderInit;

typedef struct Oot3dColliderInitType1 {
    u8 colType;
    u8 atFlags;
    u8 acFlags;
    u8 ocFlags1;
    u8 shape;
    u8 pad_005[3];
} Oot3dColliderInitType1;

typedef struct Oot3dColliderInfoInit {
    u8 elemType;
    u8 pad_001[3];
    u32 toucherDamageFlags;
    u8 toucherEffect;
    u8 toucherDamage;
    u8 pad_00A[2];
    u32 bumperDamageFlags;
    u8 bumperEffect;
    u8 bumperDefense;
    u8 pad_012[2];
    u8 toucherFlags;
    u8 bumperFlags;
    u8 ocElemFlags;
    u8 pad_017;
} Oot3dColliderInfoInit;

typedef struct Oot3dColliderCylinderInit {
    Oot3dColliderInit base;
    Oot3dColliderInfoInit info;
    Oot3dColliderCylinderDim dim;
} Oot3dColliderCylinderInit;

typedef struct Oot3dColliderCylinderInitType1 {
    Oot3dColliderInitType1 base;
    Oot3dColliderInfoInit info;
    Oot3dColliderCylinderDim dim;
} Oot3dColliderCylinderInitType1;

typedef struct Oot3dColliderJntSphDim {
    u8 model[0x10];
    Oot3dMathSphere worldSphere;
    u8 pad_020[0x08];
} Oot3dColliderJntSphDim;

typedef struct Oot3dColliderJntSphElement {
    Oot3dColliderInfo info;
    Oot3dColliderJntSphDim dim;
} Oot3dColliderJntSphElement;

typedef struct Oot3dColliderJntSphElementInit {
    Oot3dColliderInfoInit info;
    u8 dim[0x28];
} Oot3dColliderJntSphElementInit;

typedef struct Oot3dColliderTrisElement {
    Oot3dColliderInfo info;
    Oot3dMathTriNorm dim;
} Oot3dColliderTrisElement;

typedef struct Oot3dColliderJntSph {
    Oot3dCollider base;
    s32 count;
    u32 elements;
} Oot3dColliderJntSph;

typedef struct Oot3dColliderJntSphInit {
    Oot3dColliderInit base;
    s32 count;
    u32 elements;
} Oot3dColliderJntSphInit;

typedef struct Oot3dColliderTris {
    Oot3dCollider base;
    s32 count;
    u32 elements;
} Oot3dColliderTris;

typedef struct Oot3dColliderQuadDim {
    Oot3dActorVec3f vertices[4];
    Oot3dVec3s dcMid;
    Oot3dVec3s baMid;
} Oot3dColliderQuadDim;

typedef struct Oot3dColliderQuad {
    Oot3dCollider base;
    Oot3dColliderInfo info;
    Oot3dColliderQuadDim dim;
    float acDist;
} Oot3dColliderQuad;

typedef struct Oot3dColliderQuadInit {
    Oot3dColliderInit base;
    Oot3dColliderInfoInit info;
    Oot3dActorVec3f vertices[4];
} Oot3dColliderQuadInit;

typedef struct Oot3dColorRgba8 {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} Oot3dColorRgba8;

typedef struct Oot3dEffectSparkElement {
    u8 raw[0x28];
} Oot3dEffectSparkElement;

typedef struct Oot3dEffectSparkInit {
    Oot3dVec3s position;
    u8 pad_006[2];
    s32 numElements;
    Oot3dEffectSparkElement elements[32];
    float speed;
    float gravity;
    u32 uDiv;
    u32 vDiv;
    Oot3dColorRgba8 colorStart[4];
    Oot3dColorRgba8 colorEnd[4];
    s32 timer;
    s32 duration;
} Oot3dEffectSparkInit;

typedef struct Oot3dEffectShieldParticleInit {
    u8 numElements;
    u8 pad_001;
    Oot3dVec3s position;
    Oot3dColorRgba8 colors[6];
    float deceleration;
    float maxInitialSpeed;
    float lengthCutoff;
    u8 duration;
    u8 pad_02D[3];
    Oot3dActorVec3f lightPosition;
    Oot3dColorRgba8 lightColor;
    s16 lightRadius;
    u8 lightGlow;
    u8 pad_043;
    s32 lightDecay;
} Oot3dEffectShieldParticleInit;

typedef struct Oot3dCollisionCheckContext {
    s16 colATCount;
    u16 sacFlags;
    u32 colAT[OOT3D_COLLISION_AT_MAX];
    s32 colACCount;
    u32 colAC[OOT3D_COLLISION_AC_MAX];
    s32 colOCCount;
    u32 colOC[OOT3D_COLLISION_OC_MAX];
    s32 colLineCount;
    u32 colLine[3];
} Oot3dCollisionCheckContext;

#if defined(__cplusplus)
#define OOT3D_COLLISION_STATIC_ASSERT static_assert
#else
#define OOT3D_COLLISION_STATIC_ASSERT _Static_assert
#endif

OOT3D_COLLISION_STATIC_ASSERT(sizeof(Oot3dCollider) == 0x18, "Collider size");
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dCollider, atFlags) == 0x10,
    "Collider AT flags"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dCollider, shape) == 0x15,
    "Collider shape"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderInfo) == 0x28,
    "ColliderInfo size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dColliderInfo, elemType) == 0x14,
    "ColliderInfo element type"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dColliderInfo, atHit) == 0x18,
    "ColliderInfo AT hit"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderCylinderDim) == 0x18,
    "ColliderCylinderDim size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderCylinder) == 0x58,
    "ColliderCylinder size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dColliderCylinder, dim) == 0x40,
    "ColliderCylinder dimensions"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderInfoInit) == 0x18,
    "ColliderInfoInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderCylinderInit) == 0x38,
    "ColliderCylinderInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderCylinderInitType1) == 0x38,
    "ColliderCylinderInitType1 size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dColliderCylinderInit, dim) == 0x20,
    "ColliderCylinderInit dimensions"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderJntSphElement) == 0x50,
    "ColliderJntSphElement size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderJntSphElementInit) == 0x40,
    "ColliderJntSphElementInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderTrisElement) == 0x5C,
    "ColliderTrisElement size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderJntSph) == 0x20,
    "ColliderJntSph size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderJntSphInit) == 0x10,
    "ColliderJntSphInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderTris) == 0x20,
    "ColliderTris size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderQuad) == 0x80,
    "ColliderQuad size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dColliderQuadInit) == 0x50,
    "ColliderQuadInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dColliderQuad, acDist) == 0x7C,
    "ColliderQuad AC distance"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dEffectSparkInit) == 0x544,
    "EffectSparkInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dEffectShieldParticleInit) == 0x48,
    "EffectShieldParticleInit size"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dCollisionCheckContext, colACCount) == 0xCC,
    "CollisionCheck AC count"
);
OOT3D_COLLISION_STATIC_ASSERT(
    offsetof(Oot3dCollisionCheckContext, colOCCount) == 0x1C0,
    "CollisionCheck OC count"
);
OOT3D_COLLISION_STATIC_ASSERT(
    sizeof(Oot3dCollisionCheckContext) == 0x29C,
    "CollisionCheckContext size"
);

#undef OOT3D_COLLISION_STATIC_ASSERT

#ifdef __cplusplus
extern "C" {
#endif

#if defined(OOT3D_HOST_PTR32_RESOLVER)
void* oot3d_host_ptr32_resolve(u32 address);
#endif

bool FrameAdvance_IsEnabled(const Oot3dPlayState* play);

s32 Collider_InitCylinder(
    Oot3dPlayState* play,
    Oot3dColliderCylinder* collider
);
s32 Collider_InitQuad(
    Oot3dPlayState* play,
    Oot3dColliderQuad* collider
);
s32 Collider_SetJntSph(
    Oot3dPlayState* play,
    Oot3dColliderJntSph* collider,
    Oot3dActor* actor,
    const Oot3dColliderJntSphInit* init,
    Oot3dColliderJntSphElement* elements
);
s32 Collider_SetCylinder(
    Oot3dPlayState* play,
    Oot3dColliderCylinder* collider,
    Oot3dActor* actor,
    const Oot3dColliderCylinderInit* init
);
s32 Collider_SetCylinderType1(
    Oot3dPlayState* play,
    Oot3dColliderCylinder* collider,
    Oot3dActor* actor,
    const Oot3dColliderCylinderInitType1* init
);
s32 Collider_SetQuad(
    Oot3dPlayState* play,
    Oot3dColliderQuad* collider,
    Oot3dActor* actor,
    const Oot3dColliderQuadInit* init
);
void Collider_SetTrisVertices(
    Oot3dColliderTris* collider,
    s32 index,
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b,
    const Oot3dActorVec3f* c
);
s32 Collider_QuadSetNearestAC(
    Oot3dPlayState* play,
    Oot3dColliderQuad* collider,
    const Oot3dActorVec3f* hitPosition
);
s32 CollisionCheck_SetATvsAC(
    Oot3dPlayState* play,
    Oot3dCollider* atCollider,
    Oot3dColliderInfo* atInfo,
    const Oot3dActorVec3f* atPosition,
    Oot3dCollider* acCollider,
    Oot3dColliderInfo* acInfo,
    const Oot3dActorVec3f* acPosition,
    const Oot3dActorVec3f* hitPosition
);
void CollisionCheck_AC_QuadVsJntSph(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dColliderQuad* atCollider,
    Oot3dColliderJntSph* acCollider
);
void CollisionCheck_SetCylHitFX(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dColliderCylinder* collider
);
void CollisionCheck_SetQuadHitFX(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dColliderQuad* collider
);
void CollisionCheck_SetTrisHitFX(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dColliderTris* collider
);
void CollisionCheck_SetJntSphHitFX(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dColliderJntSph* collider
);
void CollisionCheck_SpawnRedBlood(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position
);
void CollisionCheck_BlueBlood(
    Oot3dPlayState* play,
    Oot3dCollider* collider,
    const Oot3dActorVec3f* position
);
void CollisionCheck_GreenBlood(
    Oot3dPlayState* play,
    Oot3dCollider* collider,
    const Oot3dActorVec3f* position
);
void CollisionCheck_WaterBurst(
    Oot3dPlayState* play,
    Oot3dCollider* collider,
    const Oot3dActorVec3f* position
);
void CollisionCheck_SpawnShieldParticles(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position,
    u32 intensity
);

s32 Collider_ResetJntSphAT(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetJntSphAC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetJntSphOC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetCylinderAT(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetCylinderAC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetCylinderOC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetTrisAT(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetTrisAC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetTrisOC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetQuadAT(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetQuadAC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);
s32 Collider_ResetQuadOC(
    Oot3dPlayState* play,
    Oot3dCollider* collider
);

s32 CollisionCheck_SetAT(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dCollider* collider
);
s32 CollisionCheck_SetAC(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dCollider* collider
);
s32 CollisionCheck_SetOC(
    Oot3dPlayState* play,
    Oot3dCollisionCheckContext* context,
    Oot3dCollider* collider
);

#ifdef __cplusplus
}
#endif

#endif
