#ifndef OOT3D_OWNER_ACTOR_RUNTIME_H
#define OOT3D_OWNER_ACTOR_RUNTIME_H

#include "oot3d/actor_spawn.h"
#include "oot3d/player.h"

typedef struct Oot3dAttentionRuntime {
    Oot3dActorVec3f naviHoverPos;
    Oot3dActorVec3f reticlePos;
    float naviColor[8];
    u32 naviHoverActor;
    u32 reticleActor;
    float naviMoveProgress;
    float reticleRadius;
    float previousReticleRadius;
    s16 reticleFadeAlpha;
    u8 naviHoverCategory;
    u8 reticleSpinCounter;
    u8 unk_50;
    u8 reticleMotionCounter;
    u8 unk_52[0x07];
    u8 reticleTargetCategory;
    u8 unk_5A[0x4A];
    u32 forcedLockOnActor;
    u8 unk_A8[0x04];
    u32 arrowHoverActor;
} Oot3dAttentionRuntime;

#if defined(OOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER)
void* oot3d_host_owner_actor_global_resolve(u32 address);
u64 oot3d_host_owner_actor_system_tick(void);
#endif

typedef void (*Oot3dOwnerRegistryResetCallback)(
    void* record,
    u32 value
);

#if defined(OOT3D_HOST_OWNER_ACTOR_REGISTRY_CALLBACK_RESOLVER)
Oot3dOwnerRegistryResetCallback
oot3d_host_owner_actor_registry_callback_resolve(void);
#endif

s16 Play_GetActiveCameraId(const Oot3dPlayState* play);
void ActorRegistry_ResetRecord(void* record, u32 value);
void ActorRegistry_ReleasePosition(const void* position);
Oot3dActor* Actor_Delete(
    Oot3dActorContext* actorContext,
    Oot3dActor* actor,
    Oot3dPlayState* play
);
void Attention_FindActorInCategory(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext,
    Oot3dPlayer* player,
    u32 category
);
void Attention_Update(
    Oot3dAttentionRuntime* attention,
    Oot3dPlayer* player,
    Oot3dActor* focusActor,
    Oot3dPlayState* play
);
void Actor_UpdateAll(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext
);

/* Address-preserving target identities. */
s16 FUN_0049f3b4(const Oot3dPlayState* play);
void FUN_004a4178(void* record, u32 value);
void FUN_0049fa78(const void* position);
void FUN_002cf684(
    Oot3dPlayState*, Oot3dActorContext*, Oot3dPlayer*, u32, s32
);
void FUN_0047976c(
    Oot3dAttentionRuntime*, Oot3dPlayer*, Oot3dActor*,
    Oot3dPlayState*
);
void FUN_00461344(
    Oot3dPlayState*, Oot3dActorContext*
);

#if defined(__cplusplus)
static_assert(
    offsetof(Oot3dAttentionRuntime, naviHoverActor) == 0x38,
    "attention Navi actor"
);
static_assert(
    offsetof(Oot3dAttentionRuntime, forcedLockOnActor) == 0xA4,
    "attention forced actor"
);
static_assert(
    offsetof(Oot3dAttentionRuntime, reticleTargetCategory) == 0x59,
    "attention reticle category"
);
static_assert(
    offsetof(Oot3dAttentionRuntime, arrowHoverActor) == 0xAC,
    "attention arrow actor"
);
#else
_Static_assert(
    offsetof(Oot3dAttentionRuntime, naviHoverActor) == 0x38,
    "attention Navi actor"
);
_Static_assert(
    offsetof(Oot3dAttentionRuntime, forcedLockOnActor) == 0xA4,
    "attention forced actor"
);
_Static_assert(
    offsetof(Oot3dAttentionRuntime, reticleTargetCategory) == 0x59,
    "attention reticle category"
);
_Static_assert(
    offsetof(Oot3dAttentionRuntime, arrowHoverActor) == 0xAC,
    "attention arrow actor"
);
#endif

#endif
