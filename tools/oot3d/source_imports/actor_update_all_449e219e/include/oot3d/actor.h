#ifndef OOT3D_ACTOR_H
#define OOT3D_ACTOR_H

#include "oot3d/types.h"

enum {
    OOT3D_ACTOR_SIZE = 0x1A4,
    OOT3D_ACTOR_FLAG_0 = 1u << 0,
};

typedef struct Oot3dActorVec3f {
    float x;
    float y;
    float z;
} Oot3dActorVec3f;

typedef struct Oot3dActorRot {
    s16 x;
    s16 y;
    s16 z;
} Oot3dActorRot;

typedef struct Oot3dActorShape {
    Oot3dActorRot rot;
    s16 face;
    float yOffset;
    u32 shadowDraw;
    float shadowScale;
    u8 shadowAlpha;
    u8 feetFloorFlags;
    u8 unk_016[2];
    Oot3dActorVec3f feetPos[2];
} Oot3dActorShape;

typedef struct Oot3dActorColChkInfo {
    u32 damageTable;
    Oot3dActorVec3f displacement;
    s16 cylinderRadius;
    s16 cylinderHeight;
    s16 cylinderYShift;
    u8 mass;
    u8 health;
    u8 damage;
    u8 damageEffect;
    u8 atHitEffect;
    u8 acHitEffect;
} Oot3dActorColChkInfo;

typedef struct Oot3dInitChainEntry {
    u32 word;
} Oot3dInitChainEntry;

typedef enum Oot3dInitChainType {
    OOT3D_ICHAIN_U8 = 0,
    OOT3D_ICHAIN_S8 = 1,
    OOT3D_ICHAIN_U16 = 2,
    OOT3D_ICHAIN_S16 = 3,
    OOT3D_ICHAIN_U32 = 4,
    OOT3D_ICHAIN_S32 = 5,
    OOT3D_ICHAIN_F32 = 6,
    OOT3D_ICHAIN_F32_DIV_1000 = 7,
    OOT3D_ICHAIN_VEC3F = 8,
    OOT3D_ICHAIN_VEC3F_DIV_1000 = 9,
    OOT3D_ICHAIN_VEC3S = 10,
} Oot3dInitChainType;

#define OOT3D_ICHAIN_ENTRY(cont, type, offset, value) \
    { \
        ((u32)(u16)(s16)(value) << 16) | \
        (((u32)(offset) & 0x7FFu) << 5) | \
        (((u32)(type) & 0xFu) << 1) | \
        ((u32)(cont) & 1u) \
    }

/*
 * Native 32-bit actor layout recovered from lifecycle, movement and collision
 * consumers. Target pointers and callback addresses remain u32 so host
 * validation preserves the original offsets.
 */
typedef struct Oot3dActor {
    s16 id;
    u8 category;
    u8 room;
    u32 flags;
    Oot3dActorVec3f homePos;
    Oot3dActorRot homeRot;
    u8 unk_01A[0x02];
    s16 params;
    u8 objectSlot;
    u8 targetMode;
    u8 bgCheckControlFlags;
    u8 unk_021[0x07];
    Oot3dActorVec3f worldPos;
    Oot3dActorRot worldRot;
    u8 unk_03A[0x02];
    Oot3dActorVec3f focusPos;
    Oot3dActorRot focusRot;
    u8 unk_04E[0x06];
    Oot3dActorVec3f scale;
    Oot3dActorVec3f velocity;
    float speedXZ;
    float gravity;
    float minVelocityY;
    u32 wallPoly;
    u32 floorPoly;
    u8 wallBgId;
    u8 floorBgId;
    s16 wallYaw;
    float floorHeight;
    float yDistToWater;
    float waterSurfaceY;
    u16 bgCheckFlags;
    s16 yawTowardsPlayer;
    float xyzDistToPlayerSq;
    float xzDistToPlayer;
    float yDistToPlayer;
    Oot3dActorColChkInfo colChkInfo;
    Oot3dActorShape shape;
    Oot3dActorVec3f projectedPos;
    float projectedW;
    float uncullZoneForward;
    float uncullZoneScale;
    float uncullZoneDownward;
    Oot3dActorVec3f prevPos;
    u8 isLockedOn;
    u8 attentionPriority;
    u8 unk_116[0x02];
    s16 freezeTimer;
    s16 colorFilterTimer;
    u32 colorFilterParams;
    u8 unk_120;
    u8 isDrawn;
    u8 unk_122;
    u8 naviEnemyId;
    u32 parent;
    u32 child;
    u32 prev;
    u32 next;
    u32 init;
    u32 destroy;
    u32 update;
    u32 draw;
    u32 overlayEntry;
    u8 unk_148[0x30];
    u32 modelContext;
    u32 ownedModelSlots[6];
    u32 lastOwnedModel;
    u8 destroyState;
    u8 unk_199;
    u8 modelsInitialized;
    u8 unk_19B;
    s16 updateTimer;
    u8 unk_19E[0x02];
    float unk_1A0;
} Oot3dActor;

#if defined(__cplusplus)
static_assert(sizeof(Oot3dActor) == OOT3D_ACTOR_SIZE, "Actor size");
static_assert(offsetof(Oot3dActor, id) == 0x00, "Actor id offset");
static_assert(offsetof(Oot3dActor, flags) == 0x04, "Actor flags offset");
static_assert(offsetof(Oot3dActor, homePos) == 0x08, "Actor home position");
static_assert(offsetof(Oot3dActor, homeRot) == 0x14, "Actor home rotation");
static_assert(offsetof(Oot3dActor, params) == 0x1C, "Actor params offset");
static_assert(
    offsetof(Oot3dActor, objectSlot) == 0x1E,
    "Actor object-slot offset"
);
static_assert(
    offsetof(Oot3dActor, worldPos) == 0x28,
    "Actor world-position offset"
);
static_assert(
    offsetof(Oot3dActor, worldRot) == 0x34,
    "Actor world-rotation offset"
);
static_assert(
    offsetof(Oot3dActor, focusPos) == 0x3C,
    "Actor focus-position offset"
);
static_assert(
    offsetof(Oot3dActor, focusRot) == 0x48,
    "Actor focus-rotation offset"
);
static_assert(offsetof(Oot3dActor, scale) == 0x54, "Actor scale offset");
static_assert(
    offsetof(Oot3dActor, velocity) == 0x60,
    "Actor velocity offset"
);
static_assert(
    offsetof(Oot3dActor, speedXZ) == 0x6C,
    "Actor speed offset"
);
static_assert(
    offsetof(Oot3dActor, gravity) == 0x70,
    "Actor gravity offset"
);
static_assert(
    offsetof(Oot3dActor, wallPoly) == 0x78,
    "Actor wall-poly offset"
);
static_assert(
    offsetof(Oot3dActor, floorPoly) == 0x7C,
    "Actor floor-poly offset"
);
static_assert(
    offsetof(Oot3dActor, floorHeight) == 0x84,
    "Actor floor-height offset"
);
static_assert(
    offsetof(Oot3dActor, bgCheckFlags) == 0x90,
    "Actor bg-check-flags offset"
);
static_assert(
    offsetof(Oot3dActor, colChkInfo) == 0xA0,
    "Actor collision-check info offset"
);
static_assert(
    offsetof(Oot3dActor, colChkInfo.displacement) == 0xA4,
    "Actor displacement offset"
);
static_assert(offsetof(Oot3dActor, shape) == 0xBC, "Actor shape offset");
static_assert(
    offsetof(Oot3dActor, projectedPos) == 0xEC,
    "Actor projected-position offset"
);
static_assert(
    offsetof(Oot3dActor, uncullZoneForward) == 0xFC,
    "Actor uncull-forward offset"
);
static_assert(
    offsetof(Oot3dActor, prevPos) == 0x108,
    "Actor previous-position offset"
);
static_assert(
    offsetof(Oot3dActor, colorFilterTimer) == 0x11A,
    "Actor color-filter timer offset"
);
static_assert(
    offsetof(Oot3dActor, isLockedOn) == 0x114,
    "Actor lock-on state offset"
);
static_assert(
    offsetof(Oot3dActor, attentionPriority) == 0x115,
    "Actor attention-priority offset"
);
static_assert(
    offsetof(Oot3dActor, freezeTimer) == 0x118,
    "Actor freeze-timer offset"
);
static_assert(
    offsetof(Oot3dActor, colorFilterParams) == 0x11C,
    "Actor color-filter params offset"
);
static_assert(
    offsetof(Oot3dActor, naviEnemyId) == 0x123,
    "Actor Navi enemy-id offset"
);
static_assert(
    offsetof(Oot3dActor, isDrawn) == 0x121,
    "Actor drawn-state offset"
);
static_assert(
    offsetof(Oot3dActor, updateTimer) == 0x19C,
    "Actor update-timer offset"
);
static_assert(offsetof(Oot3dActor, parent) == 0x124, "Actor parent offset");
static_assert(offsetof(Oot3dActor, child) == 0x128, "Actor child offset");
static_assert(offsetof(Oot3dActor, prev) == 0x12C, "Actor prev offset");
static_assert(offsetof(Oot3dActor, next) == 0x130, "Actor next offset");
static_assert(offsetof(Oot3dActor, init) == 0x134, "Actor init offset");
static_assert(
    offsetof(Oot3dActor, destroy) == 0x138,
    "Actor destroy offset"
);
static_assert(offsetof(Oot3dActor, update) == 0x13C, "Actor update offset");
static_assert(offsetof(Oot3dActor, draw) == 0x140, "Actor draw offset");
static_assert(
    offsetof(Oot3dActor, overlayEntry) == 0x144,
    "Actor overlay-entry offset"
);
static_assert(
    offsetof(Oot3dActor, modelContext) == 0x178,
    "Actor model-context offset"
);
static_assert(
    offsetof(Oot3dActor, ownedModelSlots) == 0x17C,
    "Actor owned-model slots"
);
static_assert(
    offsetof(Oot3dActor, lastOwnedModel) == 0x194,
    "Actor last owned model"
);
static_assert(
    offsetof(Oot3dActor, destroyState) == 0x198,
    "Actor destroy state"
);
static_assert(
    offsetof(Oot3dActor, modelsInitialized) == 0x19A,
    "Actor model-ready offset"
);
static_assert(sizeof(Oot3dActorShape) == 0x30, "ActorShape size");
static_assert(
    sizeof(Oot3dActorColChkInfo) == 0x1C,
    "Actor collision-check info size"
);
static_assert(sizeof(Oot3dActorRot) == 0x06, "ActorRot size");
static_assert(sizeof(Oot3dInitChainEntry) == 0x04, "InitChain entry size");
static_assert(
    offsetof(Oot3dActorShape, yOffset) == 0x08,
    "ActorShape y-offset"
);
static_assert(
    offsetof(Oot3dActorShape, shadowDraw) == 0x0C,
    "ActorShape shadow callback"
);
static_assert(
    offsetof(Oot3dActorShape, feetPos) == 0x18,
    "ActorShape feet positions"
);
#else
_Static_assert(sizeof(Oot3dActor) == OOT3D_ACTOR_SIZE, "Actor size");
_Static_assert(offsetof(Oot3dActor, id) == 0x00, "Actor id offset");
_Static_assert(offsetof(Oot3dActor, flags) == 0x04, "Actor flags offset");
_Static_assert(offsetof(Oot3dActor, homePos) == 0x08, "Actor home position");
_Static_assert(offsetof(Oot3dActor, homeRot) == 0x14, "Actor home rotation");
_Static_assert(offsetof(Oot3dActor, params) == 0x1C, "Actor params offset");
_Static_assert(
    offsetof(Oot3dActor, objectSlot) == 0x1E,
    "Actor object-slot offset"
);
_Static_assert(
    offsetof(Oot3dActor, worldPos) == 0x28,
    "Actor world-position offset"
);
_Static_assert(
    offsetof(Oot3dActor, worldRot) == 0x34,
    "Actor world-rotation offset"
);
_Static_assert(
    offsetof(Oot3dActor, focusPos) == 0x3C,
    "Actor focus-position offset"
);
_Static_assert(
    offsetof(Oot3dActor, focusRot) == 0x48,
    "Actor focus-rotation offset"
);
_Static_assert(offsetof(Oot3dActor, scale) == 0x54, "Actor scale offset");
_Static_assert(
    offsetof(Oot3dActor, velocity) == 0x60,
    "Actor velocity offset"
);
_Static_assert(
    offsetof(Oot3dActor, speedXZ) == 0x6C,
    "Actor speed offset"
);
_Static_assert(
    offsetof(Oot3dActor, gravity) == 0x70,
    "Actor gravity offset"
);
_Static_assert(
    offsetof(Oot3dActor, wallPoly) == 0x78,
    "Actor wall-poly offset"
);
_Static_assert(
    offsetof(Oot3dActor, floorPoly) == 0x7C,
    "Actor floor-poly offset"
);
_Static_assert(
    offsetof(Oot3dActor, floorHeight) == 0x84,
    "Actor floor-height offset"
);
_Static_assert(
    offsetof(Oot3dActor, bgCheckFlags) == 0x90,
    "Actor bg-check-flags offset"
);
_Static_assert(
    offsetof(Oot3dActor, colChkInfo) == 0xA0,
    "Actor collision-check info offset"
);
_Static_assert(
    offsetof(Oot3dActor, colChkInfo.displacement) == 0xA4,
    "Actor displacement offset"
);
_Static_assert(offsetof(Oot3dActor, shape) == 0xBC, "Actor shape offset");
_Static_assert(
    offsetof(Oot3dActor, projectedPos) == 0xEC,
    "Actor projected-position offset"
);
_Static_assert(
    offsetof(Oot3dActor, uncullZoneForward) == 0xFC,
    "Actor uncull-forward offset"
);
_Static_assert(
    offsetof(Oot3dActor, prevPos) == 0x108,
    "Actor previous-position offset"
);
_Static_assert(
    offsetof(Oot3dActor, colorFilterTimer) == 0x11A,
    "Actor color-filter timer offset"
);
_Static_assert(
    offsetof(Oot3dActor, isLockedOn) == 0x114,
    "Actor lock-on state offset"
);
_Static_assert(
    offsetof(Oot3dActor, attentionPriority) == 0x115,
    "Actor attention-priority offset"
);
_Static_assert(
    offsetof(Oot3dActor, freezeTimer) == 0x118,
    "Actor freeze-timer offset"
);
_Static_assert(
    offsetof(Oot3dActor, colorFilterParams) == 0x11C,
    "Actor color-filter params offset"
);
_Static_assert(
    offsetof(Oot3dActor, naviEnemyId) == 0x123,
    "Actor Navi enemy-id offset"
);
_Static_assert(
    offsetof(Oot3dActor, isDrawn) == 0x121,
    "Actor drawn-state offset"
);
_Static_assert(
    offsetof(Oot3dActor, updateTimer) == 0x19C,
    "Actor update-timer offset"
);
_Static_assert(offsetof(Oot3dActor, parent) == 0x124, "Actor parent offset");
_Static_assert(offsetof(Oot3dActor, child) == 0x128, "Actor child offset");
_Static_assert(offsetof(Oot3dActor, prev) == 0x12C, "Actor prev offset");
_Static_assert(offsetof(Oot3dActor, next) == 0x130, "Actor next offset");
_Static_assert(offsetof(Oot3dActor, init) == 0x134, "Actor init offset");
_Static_assert(
    offsetof(Oot3dActor, destroy) == 0x138,
    "Actor destroy offset"
);
_Static_assert(offsetof(Oot3dActor, update) == 0x13C, "Actor update offset");
_Static_assert(offsetof(Oot3dActor, draw) == 0x140, "Actor draw offset");
_Static_assert(
    offsetof(Oot3dActor, overlayEntry) == 0x144,
    "Actor overlay-entry offset"
);
_Static_assert(
    offsetof(Oot3dActor, modelContext) == 0x178,
    "Actor model-context offset"
);
_Static_assert(
    offsetof(Oot3dActor, ownedModelSlots) == 0x17C,
    "Actor owned-model slots"
);
_Static_assert(
    offsetof(Oot3dActor, lastOwnedModel) == 0x194,
    "Actor last owned model"
);
_Static_assert(
    offsetof(Oot3dActor, destroyState) == 0x198,
    "Actor destroy state"
);
_Static_assert(
    offsetof(Oot3dActor, modelsInitialized) == 0x19A,
    "Actor model-ready offset"
);
_Static_assert(sizeof(Oot3dActorShape) == 0x30, "ActorShape size");
_Static_assert(
    sizeof(Oot3dActorColChkInfo) == 0x1C,
    "Actor collision-check info size"
);
_Static_assert(sizeof(Oot3dActorRot) == 0x06, "ActorRot size");
_Static_assert(
    sizeof(Oot3dInitChainEntry) == 0x04,
    "InitChain entry size"
);
_Static_assert(
    offsetof(Oot3dActorShape, yOffset) == 0x08,
    "ActorShape y-offset"
);
_Static_assert(
    offsetof(Oot3dActorShape, shadowDraw) == 0x0C,
    "ActorShape shadow callback"
);
_Static_assert(
    offsetof(Oot3dActorShape, feetPos) == 0x18,
    "ActorShape feet positions"
);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void Actor_Kill(Oot3dActor* actor);
void Actor_ProcessInitChain(
    Oot3dActor* actor,
    const Oot3dInitChainEntry* chain
);
void IChain_Apply_u8(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_s8(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_u16(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_s16(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_u32(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_s32(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_f32(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_f32div1000(
    u8* ptr,
    const Oot3dInitChainEntry* entry
);
void IChain_Apply_Vec3f(u8* ptr, const Oot3dInitChainEntry* entry);
void IChain_Apply_Vec3fdiv1000(
    u8* ptr,
    const Oot3dInitChainEntry* entry
);
void IChain_Apply_Vec3s(u8* ptr, const Oot3dInitChainEntry* entry);
void ActorShape_Init(
    Oot3dActorShape* shape,
    float yOffset,
    u32 shadowDraw,
    float shadowScale
);
void Actor_SetFocus(Oot3dActor* actor, float yOffset);
void Actor_SetScale(Oot3dActor* actor, float scale);
void Actor_MoveForward(Oot3dActor* actor);

#ifdef __cplusplus
}
#endif

#endif
