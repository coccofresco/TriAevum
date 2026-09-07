#ifndef OOT3D_OWNER_CLOSURE_H
#define OOT3D_OWNER_CLOSURE_H

#include "oot3d/actor.h"
#include "oot3d/types.h"

#include <stddef.h>

typedef struct Oot3dPlayState Oot3dPlayState;
typedef struct Oot3dPlayer Oot3dPlayer;

enum {
    OOT3D_RECORD54_INITIALIZED_SIZE = 0x50,
    OOT3D_RECORD54_STRIDE = 0x54,
    OOT3D_DYNA_ACTOR_COUNT = 0x32,
    OOT3D_DYNA_DELETE_FLAGS_OFFSET = 0x151C,
    OOT3D_PLAY_DYNA_ACTOR_CONTEXT_OFFSET = 0x0A98,
    OOT3D_DYNA_ACTOR_FLAGS_OFFSET = 0x156C,
    OOT3D_DYNA_ACTOR_ENTRY_SIZE = 0x6C,
    OOT3D_DYNA_ACTOR_ENTRY_POINTER_OFFSET = 0x54,
    OOT3D_DYNA_ACTOR_INTERACT_FLAGS_OFFSET = 0x1B8,
    OOT3D_PLAYER_LOCK_ON_ACTOR_OFFSET = 0x16F8,
    OOT3D_PLAYER_STATE_FLAGS_OFFSET = 0x1714,
    OOT3D_PLAYER_LOCK_ON_FLAG = 0x00002000,
};

typedef struct Oot3dGameState {
    u8 unk_000[0x04];
    u32 main;
    u8 unk_008[0xF0];
    u32 frames;
} Oot3dGameState;

typedef struct Oot3dActorUpdateDefaultsRecord {
    u8 unk_00[0x04];
    u32 defaultWord0;
    u32 defaultWord1;
    u32 defaultWord2;
    u8 unk_10[0x08];
    u8 clearedBytes[4];
} Oot3dActorUpdateDefaultsRecord;

typedef struct Oot3dActorUpdateHalfwordRecord {
    u8 unk_00[0x04];
    u16 halfword04;
    u8 unk_06[0x06];
    u16 halfword0C;
} Oot3dActorUpdateHalfwordRecord;

typedef struct Oot3dPauseUiDualAlphaState {
    u8 unk_00[0x10];
    u8 fadeTimer;
    u8 gateTimer;
    s16 primaryAlpha;
    s16 secondaryAlpha;
} Oot3dPauseUiDualAlphaState;

typedef struct Oot3dRendererObjectRef {
    u32 object;
} Oot3dRendererObjectRef;

typedef u32 Oot3dRendererObjectHandle;

typedef struct Oot3dAudioRequestFlag100State {
    u8 unk_00[0x04];
    u8 pending;
    u8 unk_05[0x23];
    u32 requestedSfxId;
} Oot3dAudioRequestFlag100State;

typedef void (*Oot3dOwnerActorCallback)(
    Oot3dActor* actor,
    Oot3dPlayState* play
);
typedef void (*Oot3dOwnerGameStateMain)(Oot3dGameState* gameState);
typedef void (*Oot3dOwnerAllocatorDestroy)(
    void* allocator,
    void* modelContext
);
typedef void (*Oot3dOwnerResourceDestroy)(void* resource);

#if defined(OOT3D_HOST_OWNER_CLOSURE_PTR32_RESOLVER)
void* oot3d_host_owner_closure_ptr32_resolve(u32 address);
#endif

#if defined(OOT3D_HOST_OWNER_CLOSURE_CALLBACK_RESOLVER)
Oot3dOwnerActorCallback
oot3d_host_owner_actor_callback_resolve(u32 address);
Oot3dOwnerGameStateMain
oot3d_host_owner_game_state_callback_resolve(u32 address);
Oot3dOwnerAllocatorDestroy
oot3d_host_owner_allocator_callback_resolve(u32 address);
Oot3dOwnerResourceDestroy
oot3d_host_owner_resource_callback_resolve(u32 address);
#endif

/* DAT_0055A1A8 and DAT_0054ABD4 respectively. */
extern u32 gOot3dActorAllocator;
extern Oot3dAudioRequestFlag100State* gOot3dAudioRequestFlag100State;
extern u32 gOot3dActorUpdateDefaultWord;
extern u8* gOot3dGameState;

#ifdef __cplusplus
extern "C" {
#endif

void Actor_Destroy(Oot3dActor* actor, Oot3dPlayState* play);
void* Oot3d_Record54InitPrefix(void* record);
void Player_ReleaseLockOn(Oot3dPlayer* player);
void GameState_Update(Oot3dGameState* gameState);
void Oot3d_AudioRequestFlag100Callback(
    Oot3dRendererObjectRef* descriptor,
    u32 unused1,
    u32 unused2,
    Oot3dRendererObjectHandle initialHandle
);
void PauseUi_UpdateDualAlpha(
    u32 unused,
    Oot3dPauseUiDualAlphaState* state
);
void DynaPoly_ResetActorInteractFlagsIfRegistered(
    Oot3dPlayState* play,
    void* dynaContext,
    Oot3dActor* actor
);
void ActorUpdateRecord_InitializeDefaults(
    Oot3dActorUpdateDefaultsRecord* record
);
void ActorUpdateRecord_ClearHalfwords(
    Oot3dActorUpdateHalfwordRecord* record
);

/* Address-preserving aliases used by unresolved producer callers. */
void* FUN_002ffa20(void* record);
void FUN_00334354(Oot3dPlayer* player);
void FUN_00465304(
    Oot3dRendererObjectRef* descriptor,
    u32 unused1,
    u32 unused2,
    Oot3dRendererObjectHandle initialHandle
);
void FUN_0047955c(u32 unused, Oot3dPauseUiDualAlphaState* state);
void FUN_0047af24(
    Oot3dPlayState* play,
    void* dynaContext,
    Oot3dActor* actor
);
void FUN_0047c938(Oot3dActorUpdateDefaultsRecord* record);
void FUN_0047ccdc(Oot3dActorUpdateHalfwordRecord* record);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(offsetof(Oot3dGameState, main) == 0x04, "GameState main");
static_assert(offsetof(Oot3dGameState, frames) == 0xF8, "GameState frames");
static_assert(
    offsetof(Oot3dPauseUiDualAlphaState, fadeTimer) == 0x10,
    "pause fade timer"
);
static_assert(
    offsetof(Oot3dPauseUiDualAlphaState, secondaryAlpha) == 0x14,
    "pause secondary alpha"
);
#else
_Static_assert(offsetof(Oot3dGameState, main) == 0x04, "GameState main");
_Static_assert(offsetof(Oot3dGameState, frames) == 0xF8, "GameState frames");
_Static_assert(
    offsetof(Oot3dPauseUiDualAlphaState, fadeTimer) == 0x10,
    "pause fade timer"
);
_Static_assert(
    offsetof(Oot3dPauseUiDualAlphaState, secondaryAlpha) == 0x14,
    "pause secondary alpha"
);
#endif

#endif
