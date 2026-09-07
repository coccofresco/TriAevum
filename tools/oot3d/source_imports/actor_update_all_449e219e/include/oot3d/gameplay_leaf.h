#ifndef OOT3D_GAMEPLAY_LEAF_H
#define OOT3D_GAMEPLAY_LEAF_H

#include <stdbool.h>

#include "oot3d/actor.h"
#include "oot3d/actor_bgcheck.h"
#include "oot3d/collision_check.h"
#include "oot3d/skel_anime.h"

typedef struct Oot3dPlayState Oot3dPlayState;

enum {
    OOT3D_GAMEPLAY_CAMERA_LIST_OFFSET = 0x0A54,
    OOT3D_GAMEPLAY_ACTIVE_CAMERA_OFFSET = 0x0A64,
    OOT3D_GAMEPLAY_PLAYER_OFFSET = 0x20AC,
    OOT3D_GAMEPLAY_MESSAGE_MODE_OFFSET = 0x2A90,
    OOT3D_GAMEPLAY_PATH_LIST_OFFSET = 0x5C20,
    OOT3D_CAMERA_STATUS_OFFSET = 0x188,
};

typedef struct Oot3dCollisionCheckInfoInit {
    u8 health;
    u8 pad_01;
    s16 cylinderRadius;
    s16 cylinderHeight;
    u8 mass;
    u8 pad_07;
} Oot3dCollisionCheckInfoInit;

typedef struct Oot3dCollisionCheckInfoInit2 {
    u8 health;
    u8 pad_01;
    s16 cylinderRadius;
    s16 cylinderHeight;
    s16 cylinderYShift;
    u8 mass;
    u8 pad_09;
} Oot3dCollisionCheckInfoInit2;

typedef struct Oot3dLightInfo {
    u8 type;
    u8 pad_01[3];
    Oot3dActorVec3f position;
    u8 color[3];
    u8 pad_13;
    s16 radius;
    u8 attenuation;
    u8 pad_17;
} Oot3dLightInfo;

#if defined(__cplusplus)
static_assert(sizeof(Oot3dCollisionCheckInfoInit) == 0x08, "ColChk init");
static_assert(sizeof(Oot3dCollisionCheckInfoInit2) == 0x0A, "ColChk init2");
static_assert(sizeof(Oot3dLightInfo) == 0x18, "LightInfo size");
#else
_Static_assert(sizeof(Oot3dCollisionCheckInfoInit) == 0x08, "ColChk init");
_Static_assert(sizeof(Oot3dCollisionCheckInfoInit2) == 0x0A, "ColChk init2");
_Static_assert(sizeof(Oot3dLightInfo) == 0x18, "LightInfo size");
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(OOT3D_HOST_GAMEPLAY_PTR32_RESOLVER)
void* oot3d_host_gameplay_ptr32_resolve(u32 address);
#endif

void DynaPolyActor_Init(Oot3dDynaPolyActor* actor, u32 transformFlags);
bool DynaPoly_IsBgIdBgActor(u32 bgId);

void* Gameplay_GetCamera(Oot3dPlayState* play, s32 cameraId);
void Gameplay_ChangeCameraStatus(
    Oot3dPlayState* play,
    s32 cameraId,
    s16 status
);
s32 Camera_SetParam(
    void* camera,
    s32 viewFlag,
    const void* parameter
);
s32 Gameplay_CameraSetAtEye(
    Oot3dPlayState* play,
    s32 cameraId,
    const Oot3dActorVec3f* at,
    const Oot3dActorVec3f* eye
);
s32 Gameplay_CreateSubCamera(Oot3dPlayState* play);
void Gameplay_ClearCamera(Oot3dPlayState* play, s32 cameraId);
void Gameplay_TriggerVoidOut(Oot3dPlayState* play);
void EnGirlA_Update2(void* actor, Oot3dPlayState* play);
void EnYukabyun_Break(
    Oot3dActor* actor,
    Oot3dPlayState* play
);
void EffectSsFhgFlash_SpawnLightBall(
    Oot3dPlayState* play,
    const Oot3dActorVec3f* position,
    const Oot3dActorVec3f* velocity,
    const Oot3dActorVec3f* acceleration,
    s16 scale,
    u8 parameters
);
void OnePointCutscene_AttentionSetSfx(
    Oot3dPlayState* play,
    void* actor,
    u32 sfxId,
    s32 timer,
    s32 cameraId
);
s16 Camera_ChangeStatus(void* camera, s16 status);
s16 Camera_GetCamDirYaw(const void* camera);
void Camera_SetCameraData(
    void* camera,
    u32 flags,
    u32 data0,
    u32 data1,
    s16 data2,
    s16 data3
);
void OnePointCutscene_EndCutscene(Oot3dPlayState* play, s32 cameraId);

s32 Collider_InitJntSph(
    Oot3dPlayState* play,
    Oot3dColliderJntSph* collider
);
s32 Collider_DestroyJntSph(
    Oot3dPlayState* play,
    Oot3dColliderJntSph* collider
);
s32 Collider_InitTris(
    Oot3dPlayState* play,
    Oot3dColliderTris* collider
);
s32 Collider_DestroyTris(
    Oot3dPlayState* play,
    Oot3dColliderTris* collider
);
void CollisionCheck_SetInfo(
    Oot3dActorColChkInfo* info,
    u32 damageTable,
    const Oot3dCollisionCheckInfoInit* init
);
void CollisionCheck_SetInfo2(
    Oot3dActorColChkInfo* info,
    u32 damageTable,
    const Oot3dCollisionCheckInfoInit2* init
);

void Lights_PointNoGlowSetInfo(
    Oot3dLightInfo* info,
    float x,
    float y,
    float z,
    u8 red,
    u8 green,
    u8 blue,
    s16 radius,
    u8 attenuation
);
void Lights_PointSetColorAndRadius(
    Oot3dLightInfo* info,
    u8 red,
    u8 green,
    u8 blue,
    s16 radius,
    u8 attenuation
);

enum {
    OOT3D_LIGHT_NODE_CAPACITY = 32,
};

typedef struct Oot3dLightNode {
    u32 info;
    u32 previous;
    u32 next;
} Oot3dLightNode;

typedef struct Oot3dLightNodePool {
    u32 used;
    u32 cursor;
    Oot3dLightNode nodes[OOT3D_LIGHT_NODE_CAPACITY];
} Oot3dLightNodePool;

u32 Oot3d_LightContextInsertFromPool(
    Oot3dLightNodePool* pool,
    u32 poolAddress,
    void* lightContext,
    u32 infoAddress
);
u32 LightContext_InsertLight(
    Oot3dPlayState* play,
    void* lightContext,
    Oot3dLightInfo* info
);

bool Actor_IsFacingPlayer(const Oot3dActor* actor, s16 maxAngle);
void* Actor_GetCollidedExplosive(
    Oot3dPlayState* play,
    void* colliderInfo
);
Oot3dDynaPolyActor* DynaPoly_GetActor(void* colCtx, s32 bgId);

u8 Player_GetMask(const Oot3dPlayState* play);
u8 GetMessageMode(const Oot3dPlayState* play);
void SkelAnime_SetCurrentFrame(
    Oot3dSkelAnime* skelAnime,
    float currentFrame
);
bool Player_HoldsHookshot(const void* player);
s32 Player_GetExplosiveHeld(const void* player);
void* Path_GetByIndex(Oot3dPlayState* play, s16 index, s16 max);

#ifdef __cplusplus
}
#endif

#endif
