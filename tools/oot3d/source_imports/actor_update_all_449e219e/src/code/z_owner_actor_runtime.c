#include "oot3d/owner_actor_runtime.h"

#include "oot3d/audio.h"
#include "oot3d/game_math.h"
#include "oot3d/gameplay_leaf.h"
#include "oot3d/model_render.h"
#include "oot3d/owner_camera_mode_runtime.h"
#include "oot3d/owner_certified_player_runtime.h"
#include "oot3d/owner_closure.h"
#include "oot3d/owner_projection_runtime.h"
#include "oot3d/runtime_helpers.h"
#include "oot3d/runtime_mass_recovery.h"
#include "oot3d/runtime_structural_families.h"
#include "oot3d/static_init.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    OOT3D_OWNER_ATTENTION_STATE = 0x0050C8E8,
    OOT3D_OWNER_ATTENTION_COLORS = 0x0050C998,
    OOT3D_OWNER_ATTENTION_RANGES = 0x0050CA34,
    OOT3D_OWNER_FREEZE_MASKS = 0x0050CCF4,
    OOT3D_OWNER_CATEGORY_ORDER = 0x0050CD24,
    OOT3D_OWNER_FRAME_STATE_PTR = 0x0051B2F4,
    OOT3D_OWNER_REGISTRY_STATE = 0x0054ABB4,
    OOT3D_OWNER_AUDIO_SCALE = 0x0054AC20,
    OOT3D_OWNER_AUDIO_REVERB = 0x0054AC24,
    OOT3D_OWNER_ACTOR_UPDATE_RECORD = 0x005C1858,
    OOT3D_OWNER_REGISTRY_RECORDS = 0x005AFE08,
    OOT3D_OWNER_REGISTRY_COUNT = 32,
    OOT3D_OWNER_REGISTRY_STRIDE = 0xA0,
    OOT3D_OWNER_REGISTRY_KEY = 0x84,
    OOT3D_OWNER_PLAY_PLAYER = 0x20AC,
    OOT3D_OWNER_PLAY_ACTOR_CONTEXT = 0x208C,
    OOT3D_OWNER_PLAY_DYNA_CONTEXT = 0x0AE8,
    OOT3D_OWNER_PLAY_OBJECT_CONTEXT = 0x3A58,
    OOT3D_OWNER_PLAY_ROOM = 0x4C30,
    OOT3D_OWNER_PLAY_ROOM_CLEAR = 0x2240,
    OOT3D_OWNER_PLAY_UPDATE_RATE = 0x0110,
    OOT3D_OWNER_PLAYER_FOCUS_ACTOR = 0x16F8,
    OOT3D_OWNER_PLAYER_TARGET_TIMER = 0x1700,
    OOT3D_OWNER_PLAYER_STATE_FLAGS1 = 0x1710,
    OOT3D_OWNER_PLAYER_STATE_FLAGS2 = 0x1714,
    OOT3D_OWNER_PLAYER_NAVI_ACTOR = 0x1724,
    OOT3D_OWNER_PLAYER_TALK_ACTOR = 0x172C,
    OOT3D_OWNER_PLAYER_HELD_ACTOR = 0x1224,
    OOT3D_OWNER_PLAYER_STICK_INDEX = 0x222A,
    OOT3D_OWNER_PLAYER_STICK_DIRECTIONS = 0x2231,
    OOT3D_OWNER_ATTENTION_OFFSET = 0x006C,
    OOT3D_OWNER_TITLE_CARD_OFFSET = 0x01C0,
    OOT3D_OWNER_DYNA_FLAGS = 0x151C,
    OOT3D_OWNER_DYNA_STRIDE = 0x6C,
    OOT3D_OWNER_DYNA_COUNT = 0x32,
};

typedef struct OwnerAttentionSearchState {
    u8 unk_000[0x02];
    s16 playerRotY;
    u8 unk_004[0x98];
    u32 nearestActor;
    u32 prioritizedActor;
    float nearestDistanceSq;
    float bgmEnemyDistanceSq;
    s32 highestPriority;
} OwnerAttentionSearchState;

static void* OwnerActor_ResolveGlobal(u32 address) {
#if defined(OOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER)
    return oot3d_host_owner_actor_global_resolve(address);
#else
    return (void*)(uintptr_t)address;
#endif
}

static void* OwnerActor_ResolvePointer(u32 address) {
    return oot3d_actor_spawn_ptr32_to_host(address);
}

static u32 OwnerActor_EncodePointer(const void* pointer) {
    return oot3d_actor_spawn_ptr32_from_host(pointer);
}

static Oot3dActorCallback OwnerActor_ResolveCallback(u32 address) {
#if defined(OOT3D_HOST_ACTOR_SPAWN_PTR32_RESOLVER)
    return oot3d_host_actor_spawn_callback_resolve(address);
#else
    return (Oot3dActorCallback)(uintptr_t)address;
#endif
}

static u8 OwnerActor_ReadU8(const void* object, size_t offset) {
    u8 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static s8 OwnerActor_ReadS8(const void* object, size_t offset) {
    s8 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static u16 OwnerActor_ReadU16(const void* object, size_t offset) {
    u16 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static s16 OwnerActor_ReadS16(const void* object, size_t offset) {
    s16 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static u32 OwnerActor_ReadU32(const void* object, size_t offset) {
    u32 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static s32 OwnerActor_ReadS32(const void* object, size_t offset) {
    s32 value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static float OwnerActor_ReadF32(const void* object, size_t offset) {
    float value;
    memcpy(&value, (const u8*)object + offset, sizeof(value));
    return value;
}

static void OwnerActor_WriteU8(void* object, size_t offset, u8 value) {
    memcpy((u8*)object + offset, &value, sizeof(value));
}

static void OwnerActor_WriteU32(
    void* object,
    size_t offset,
    u32 value
) {
    memcpy((u8*)object + offset, &value, sizeof(value));
}

static void OwnerActor_WriteF32(
    void* object,
    size_t offset,
    float value
) {
    memcpy((u8*)object + offset, &value, sizeof(value));
}

static u64 OwnerActor_SystemTick(void) {
#if defined(OOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER)
    return oot3d_host_owner_actor_system_tick();
#elif defined(__arm__) && defined(__GNUC__)
    register u32 low __asm__("r0");
    register u32 high __asm__("r1");
    __asm__ volatile("svc 0x28" : "=r"(low), "=r"(high));
    return ((u64)high << 32) | low;
#else
    return 0;
#endif
}

static Oot3dActor* OwnerActor_DecodeActor(u32 address) {
    return address == 0
        ? NULL
        : (Oot3dActor*)OwnerActor_ResolvePointer(address);
}

static Oot3dPlayer* OwnerActor_GetPlayer(Oot3dPlayState* play) {
    return (Oot3dPlayer*)OwnerActor_ResolvePointer(
        OwnerActor_ReadU32(play, OOT3D_OWNER_PLAY_PLAYER)
    );
}

s16 Play_GetActiveCameraId(const Oot3dPlayState* play) {
    return OwnerActor_ReadS16(play, 0x0A64);
}

s16 FUN_0049f3b4(const Oot3dPlayState* play) {
    return Play_GetActiveCameraId(play);
}

void ActorRegistry_ResetRecord(void* record, u32 value) {
    u32 index;

    for (index = 0; index < 4; ++index) {
#if defined(OOT3D_HOST_OWNER_ACTOR_REGISTRY_CALLBACK_RESOLVER)
        Oot3dOwnerRegistryResetCallback reset =
            oot3d_host_owner_actor_registry_callback_resolve();
        reset((u8*)record + 8u + index * 0x10u, value);
#else
        extern void FUN_0030cb90(void* record, u32 value);
        FUN_0030cb90((u8*)record + 8u + index * 0x10u, value);
#endif
    }
}

void FUN_004a4178(void* record, u32 value) {
    ActorRegistry_ResetRecord(record, value);
}

void ActorRegistry_ReleasePosition(const void* position) {
    u8* state = (u8*)OwnerActor_ResolveGlobal(
        OOT3D_OWNER_REGISTRY_STATE
    );
    u8* records = (u8*)OwnerActor_ResolveGlobal(
        OOT3D_OWNER_REGISTRY_RECORDS
    );
    u32 mask = OwnerActor_ReadU32(state, 0x14);
    const u32 key = OwnerActor_EncodePointer(position);
    u32 index = 0;

    while (mask != 0 && index < OOT3D_OWNER_REGISTRY_COUNT) {
        u8* record =
            records + index * OOT3D_OWNER_REGISTRY_STRIDE;
        if (
            OwnerActor_ReadU32(record, OOT3D_OWNER_REGISTRY_KEY) ==
            key
        ) {
            ActorRegistry_ResetRecord(record, 0);
            OwnerActor_WriteU32(
                record, OOT3D_OWNER_REGISTRY_KEY, 0
            );
            OwnerActor_WriteU32(
                state, 0x14,
                OwnerActor_ReadU32(state, 0x14) &
                    ~(1u << index)
            );
        }
        ++index;
        mask >>= 1;
    }
}

void FUN_0049fa78(const void* position) {
    ActorRegistry_ReleasePosition(position);
}

static void OwnerActor_ReleaseOverlay(
    Oot3dActorOverlayEntry* overlay
) {
    if (
        overlay == NULL ||
        overlay->loadedRamAddr == 0 ||
        overlay->numLoaded == 0
    ) {
        return;
    }
    --overlay->numLoaded;
    if (
        overlay->numLoaded == 0 &&
        (overlay->flags & 2u) == 0
    ) {
        if ((overlay->flags & 1u) == 0) {
            ZeldaArena_Free(
                OwnerActor_ResolvePointer(overlay->loadedRamAddr)
            );
        }
        overlay->loadedRamAddr = 0;
    }
}

Oot3dActor* Actor_Delete(
    Oot3dActorContext* actorContext,
    Oot3dActor* actor,
    Oot3dPlayState* play
) {
    Oot3dActorOverlayEntry* overlay =
        (Oot3dActorOverlayEntry*)OwnerActor_ResolvePointer(
            actor->overlayEntry
        );
    Oot3dActor* next = OwnerActor_DecodeActor(actor->next);
    Oot3dActor* previous = OwnerActor_DecodeActor(actor->prev);
    Oot3dPlayer* player = OwnerActor_GetPlayer(play);
    const u32 actorAddress = OwnerActor_EncodePointer(actor);
    const u32 nextAddress = actor->next;
    const u32 previousAddress = actor->prev;
    Oot3dActorListEntry* list =
        &actorContext->lists[actor->category];

    if (
        player != NULL &&
        OwnerActor_ReadU32(
            player, OOT3D_OWNER_PLAYER_FOCUS_ACTOR
        ) == actorAddress
    ) {
        void* camera;
        FUN_00334354(player);
        camera = Gameplay_GetCamera(
            play, Play_GetActiveCameraId(play)
        );
        Camera_ChangeMode(camera, 0);
    }
    if (OwnerActor_ReadU32(actorContext, 0xA4) == actorAddress) {
        OwnerActor_WriteU32(actorContext, 0xA4, 0);
    }
    if (OwnerActor_ReadU32(actorContext, 0x110) == actorAddress) {
        OwnerActor_WriteU32(actorContext, 0x110, 0);
    }
    if (OwnerActor_ReadU32(actorContext, 0x114) == actorAddress) {
        OwnerActor_WriteU32(actorContext, 0x114, 0);
    }

    ActorRegistry_ReleasePosition(&actor->worldPos);
    Actor_Destroy(actor, play);
    --actorContext->total;
    --list->count;
    if (previous == NULL) {
        list->head = nextAddress;
    } else {
        previous->next = nextAddress;
    }
    if (next != NULL) {
        next->prev = previousAddress;
    }
    actor->next = 0;
    actor->prev = 0;

    if (
        actor->room ==
            OwnerActor_ReadS8(play, OOT3D_OWNER_PLAY_ROOM) &&
        actor->category == 5 &&
        list->count == 0
    ) {
        const u8 room =
            OwnerActor_ReadU8(play, OOT3D_OWNER_PLAY_ROOM);
        OwnerActor_WriteU32(
            play,
            OOT3D_OWNER_PLAY_ROOM_CLEAR,
            OwnerActor_ReadU32(
                play, OOT3D_OWNER_PLAY_ROOM_CLEAR
            ) | (1u << room)
        );
    }

    ZeldaArena_Free(actor);
    OwnerActor_ReleaseOverlay(overlay);
    return next;
}

static float OwnerAttention_WeightedDistance(
    const Oot3dActor* actor,
    const Oot3dPlayer* player,
    s16 playerRotY,
    s32 alternateTargeting
) {
    const s16 relativeYaw =
        (s16)((actor->yawTowardsPlayer - (s16)0x8000) -
              playerRotY);
    s32 absoluteYaw = relativeYaw < 0
        ? -(s32)relativeYaw
        : (s32)relativeYaw;
    float distance = actor->xyzDistToPlayerSq;
    const u32 focus =
        OwnerActor_ReadU32(
            player, OOT3D_OWNER_PLAYER_FOCUS_ACTOR
        );

    if (alternateTargeting == 0) {
        if (focus == 0) {
            if (absoluteYaw > 0x2AAA) {
                return FLT_MAX;
            }
        } else {
            s32 accepted = absoluteYaw == 0x4000;
            if (absoluteYaw <= 0x4000) {
                accepted = (actor->flags & 0x08000000u) == 0;
            }
            if (!accepted) {
                return FLT_MAX;
            }
        }
        return distance;
    }

    if (focus == 0) {
        const float currentLimit = OwnerActor_ReadF32(
            OwnerActor_ResolveGlobal(OOT3D_OWNER_ATTENTION_STATE),
            0x64
        );
        if (distance >= currentLimit) {
            s32 angle = absoluteYaw;
            if (angle > 0x4000) {
                angle = (s16)(angle - 0x4000);
            }
            distance -=
                distance * OwnerActor_ReadF32(
                    OwnerActor_ResolveGlobal(
                        OOT3D_OWNER_ATTENTION_STATE
                    ),
                    0x68
                ) * (float)(0x4000 - angle) *
                0x1.0p-15f;
        } else if (absoluteYaw > 0x2AAA) {
            return FLT_MAX;
        }
    } else {
        if ((actor->flags & 0x08000000u) != 0) {
            return FLT_MAX;
        }
        {
            const float currentLimit = OwnerActor_ReadF32(
                OwnerActor_ResolveGlobal(
                    OOT3D_OWNER_ATTENTION_STATE
                ),
                0x64
            );
            if (distance < currentLimit) {
                if (absoluteYaw > 0x4000) {
                    absoluteYaw =
                        (s16)(relativeYaw - 0x4000);
                }
            } else if (absoluteYaw > 0x4000) {
                return FLT_MAX;
            }
        }
        distance -=
            distance * 0.8f *
            (float)(0x4000 - absoluteYaw) *
            0x1.0p-15f;
    }
    return distance;
}

static s32 OwnerAttention_ActorOnScreen(
    const Oot3dPlayState* play,
    const Oot3dActor* actor
) {
    s16 x;
    s16 y;

    Actor_GetScreenPos(play, actor, &x, &y);
    return x > -40 && x < 440 && y > -160 && y < 400;
}

static void OwnerAttention_FindActorInCategory(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext,
    Oot3dPlayer* player,
    u32 category,
    s32 alternateTargeting
) {
    OwnerAttentionSearchState* search =
        (OwnerAttentionSearchState*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_ATTENTION_STATE
        );
    Oot3dActor* actor = OwnerActor_DecodeActor(
        actorContext->lists[category].head
    );
    Oot3dActor* currentFocus = OwnerActor_DecodeActor(
        OwnerActor_ReadU32(
            player, OOT3D_OWNER_PLAYER_FOCUS_ACTOR
        )
    );
    while (actor != NULL) {
        if (
            actor->update != 0 &&
            actor != (Oot3dActor*)player &&
            (actor->flags & 1u) != 0
        ) {
            if (
                category == 5 &&
                (
                    actor->id != 0x1C ||
                    OwnerActor_ReadS16(actor, 0x4AE) != 0
                ) &&
                (actor->flags & 5u) == 5u &&
                actor->xyzDistToPlayerSq < 250000.0f &&
                actor->xyzDistToPlayerSq <
                    search->bgmEnemyDistanceSq
            ) {
                OwnerActor_WriteU32(
                    actorContext, 0x114,
                    OwnerActor_EncodePointer(actor)
                );
                search->bgmEnemyDistanceSq =
                    actor->xyzDistToPlayerSq;
            }
            if (actor != currentFocus) {
                const float distance =
                    OwnerAttention_WeightedDistance(
                        actor, player, search->playerRotY,
                        alternateTargeting
                    );
                const s8 targetMode = (s8)actor->targetMode;
                const float* rangeTable =
                    (const float*)OwnerActor_ResolveGlobal(
                        OOT3D_OWNER_ATTENTION_RANGES
                    );
                if (
                    distance < search->nearestDistanceSq &&
                    distance <= rangeTable[(s32)targetMode * 2] &&
                    OwnerAttention_ActorOnScreen(play, actor)
                ) {
                    Oot3dActorVec3f intersection;
                    u32 poly = 0;
                    s32 bgId = 0;
                    const s32 blocked = FUN_003723c0(
                        (u8*)play + OOT3D_PLAY_OFFSET_COLCTX,
                        (const Oot3dActorVec3f*)
                            ((const u8*)player + 0x3C),
                        &actor->focusPos,
                        &intersection,
                        &poly,
                        1, 1, 1, 1,
                        &bgId
                    );
                    if (
                        blocked == 0 ||
                        FUN_0035fe90(
                            (u8*)play + OOT3D_PLAY_OFFSET_COLCTX,
                            OwnerActor_ResolvePointer(poly),
                            (u32)bgId
                        ) != 0
                    ) {
                        if (actor->attentionPriority != 0) {
                            if (
                                actor->attentionPriority <
                                search->highestPriority
                            ) {
                                search->prioritizedActor =
                                    OwnerActor_EncodePointer(actor);
                                search->highestPriority =
                                    actor->attentionPriority;
                            }
                        } else {
                            search->nearestActor =
                                OwnerActor_EncodePointer(actor);
                            search->nearestDistanceSq = distance;
                        }
                    }
                }
            }
        }
        actor = OwnerActor_DecodeActor(actor->next);
    }
}

void Attention_FindActorInCategory(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext,
    Oot3dPlayer* player,
    u32 category
) {
    OwnerAttention_FindActorInCategory(
        play, actorContext, player, category,
        OwnerActor_ReadS8(play, 0x2148)
    );
}

void FUN_002cf684(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext,
    Oot3dPlayer* player,
    u32 category,
    s32 alternateTargeting
) {
    OwnerAttention_FindActorInCategory(
        play, actorContext, player, category,
        alternateTargeting
    );
}

static Oot3dActor* OwnerAttention_FindActor(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext,
    Oot3dPlayer* player
) {
    OwnerAttentionSearchState* search =
        (OwnerAttentionSearchState*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_ATTENTION_STATE
        );
    const u8* order = (const u8*)OwnerActor_ResolveGlobal(
        OOT3D_OWNER_CATEGORY_ORDER
    );
    const s32 alternateTargeting =
        OwnerActor_ReadS8(play, 0x2148);
    u32 index = 0;

    search->nearestActor = 0;
    search->prioritizedActor = 0;
    search->nearestDistanceSq = FLT_MAX;
    search->bgmEnemyDistanceSq = FLT_MAX;
    search->highestPriority = INT32_MAX;
    if (!Player_InCsMode(play)) {
        OwnerActor_WriteU32(actorContext, 0x114, 0);
        search->playerRotY =
            ((Oot3dActor*)player)->shape.rot.y;
        for (index = 0; index < 3; ++index) {
            OwnerAttention_FindActorInCategory(
                play, actorContext, player, order[index],
                alternateTargeting
            );
        }
        if (search->nearestActor == 0) {
            for (; index < 12; ++index) {
                OwnerAttention_FindActorInCategory(
                    play, actorContext, player, order[index], 0
                );
            }
        }
    }
    return OwnerActor_DecodeActor(
        search->nearestActor != 0
            ? search->nearestActor
            : search->prioritizedActor
    );
}

static float OwnerAttention_FrameScale(void) {
    const u32 stateAddress = OwnerActor_ReadU32(
        OwnerActor_ResolveGlobal(OOT3D_OWNER_FRAME_STATE_PTR),
        0
    );
    if (stateAddress == 0) {
        return 1.0f;
    }
    return (float)OwnerActor_ReadS16(
        OwnerActor_ResolvePointer(stateAddress),
        OOT3D_OWNER_PLAY_UPDATE_RATE
    ) / 3.0f;
}

static void OwnerAttention_SetNaviState(
    Oot3dAttentionRuntime* attention,
    const Oot3dActor* actor,
    u8 category
) {
    const u8* colors =
        (const u8*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_ATTENTION_COLORS
        ) + (size_t)category * 8u;
    u32 index;

    attention->naviHoverPos.x = actor->focusPos.x;
    attention->naviHoverPos.y =
        actor->focusPos.y +
        OwnerActor_ReadF32(actor, 0x50) * actor->scale.y;
    attention->naviHoverPos.z = actor->focusPos.z;
    for (index = 0; index < 8; ++index) {
        attention->naviColor[index] = (float)colors[index];
    }
}

static void OwnerAttention_StartReticle(
    Oot3dAttentionRuntime* attention,
    const Oot3dActor* actor,
    Oot3dPlayState* play
) {
    const u8* constants = (const u8*)OwnerActor_ResolveGlobal(
        OOT3D_OWNER_ATTENTION_STATE
    );
    const u8* colors =
        (const u8*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_ATTENTION_COLORS
        ) + (size_t)actor->category * 8u;
    u32 index;

    memcpy(&attention->reticlePos, (u8*)play + 0x1B8, 12);
    attention->reticleRadius =
        OwnerActor_ReadF32(constants, 0x04);
    attention->previousReticleRadius =
        attention->reticleRadius;
    attention->reticleTargetCategory =
        OwnerActor_ReadU8(constants, 0x18);
    attention->reticleMotionCounter = 0;
    attention->reticleFadeAlpha = 0x100;
    for (index = 0; index < 3; ++index) {
        u8* reticle = (u8*)attention + 0x5C + index * 0x18;
        OwnerActor_WriteF32(reticle, 0, 0.0f);
        OwnerActor_WriteF32(reticle, 4, 0.0f);
        OwnerActor_WriteF32(reticle, 8, 0.0f);
        OwnerActor_WriteF32(
            reticle, 0x14,
            OwnerActor_ReadF32(constants, 0x30)
        );
        OwnerActor_WriteF32(
            reticle, 0x0C, attention->reticleRadius
        );
        memcpy(reticle + 0x10, colors, 3);
    }
    attention->reticleActor = OwnerActor_EncodePointer(actor);
    if (actor->id == 0x32) {
        attention->reticleFadeAlpha = 0;
    }
    Audio_PlaySoundGeneral(
        (actor->flags & 5u) == 5u
            ? 0x010004B5u
            : 0x01000495u,
        NULL,
        4,
        (float*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_AUDIO_SCALE
        ),
        (float*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_AUDIO_SCALE
        ),
        (s8*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_AUDIO_REVERB
        )
    );
}

void Attention_Update(
    Oot3dAttentionRuntime* attention,
    Oot3dPlayer* player,
    Oot3dActor* focusActor,
    Oot3dPlayState* play
) {
    const u8* constants = (const u8*)OwnerActor_ResolveGlobal(
        OOT3D_OWNER_ATTENTION_STATE
    );
    Oot3dActor* actor = NULL;
    u8 category;

    if (
        OwnerActor_ReadU32(
            player, OOT3D_OWNER_PLAYER_FOCUS_ACTOR
        ) != 0 &&
        OwnerActor_ReadU8(
            player,
            OOT3D_OWNER_PLAYER_STICK_DIRECTIONS +
                OwnerActor_ReadU8(
                    player, OOT3D_OWNER_PLAYER_STICK_INDEX
                )
        ) == 2
    ) {
        attention->arrowHoverActor = 0;
    } else {
        actor = OwnerAttention_FindActor(
            play,
            (Oot3dActorContext*)((u8*)play +
                OOT3D_OWNER_PLAY_ACTOR_CONTEXT),
            player
        );
        attention->arrowHoverActor =
            OwnerActor_EncodePointer(actor);
    }

    if (attention->forcedLockOnActor != 0) {
        actor = OwnerActor_DecodeActor(
            attention->forcedLockOnActor
        );
        attention->forcedLockOnActor = 0;
    } else if (focusActor != NULL) {
        actor = focusActor;
    }
    category = actor != NULL
        ? actor->category
        : ((Oot3dActor*)player)->category;
    if (
        attention->naviHoverActor !=
            OwnerActor_EncodePointer(actor) ||
        attention->naviHoverCategory != category
    ) {
        attention->naviHoverActor =
            OwnerActor_EncodePointer(actor);
        attention->naviHoverCategory = category;
        attention->naviMoveProgress = 1.0f;
    }
    if (actor == NULL) {
        actor = (Oot3dActor*)player;
    }

    if (
        !Math_StepToF(
            &attention->naviMoveProgress, 0.0f, 0.25f
        )
    ) {
        const float scale =
            OwnerAttention_FrameScale() * 0.25f /
            attention->naviMoveProgress;
        attention->naviHoverPos.x +=
            (actor->worldPos.x - attention->naviHoverPos.x) *
            scale;
        attention->naviHoverPos.y +=
            (
                actor->worldPos.y +
                OwnerActor_ReadF32(actor, 0x50) *
                    actor->scale.y -
                attention->naviHoverPos.y
            ) * scale;
        attention->naviHoverPos.z +=
            (actor->worldPos.z - attention->naviHoverPos.z) *
            scale;
    } else {
        OwnerAttention_SetNaviState(
            attention, actor, category
        );
    }

    if (focusActor != NULL && attention->reticleSpinCounter == 0) {
        Oot3dActorVec3f projected;
        float inverseW;
        FUN_00368cc0(
            play, &focusActor->focusPos,
            &projected, &inverseW
        );
        if (
            projected.z <= 0.0f ||
            fabsf(projected.x * inverseW) >= 1.0f ||
            fabsf(projected.y * inverseW) >= 1.0f
        ) {
            focusActor = NULL;
        }
    }
    if (
        focusActor != NULL &&
        (
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_STATE_FLAGS1
            ) & 0x20000040u
        ) == 0x20000040u
    ) {
        focusActor = NULL;
    }

    if (focusActor != NULL) {
        if (
            attention->reticleActor !=
            OwnerActor_EncodePointer(focusActor)
        ) {
            OwnerAttention_StartReticle(
                attention, focusActor, play
            );
        }
        attention->reticlePos.x = focusActor->worldPos.x;
        attention->reticlePos.y =
            focusActor->worldPos.y -
            focusActor->shape.yOffset * focusActor->scale.y;
        attention->reticlePos.z = focusActor->worldPos.z;
        if (attention->reticleSpinCounter != 0) {
            attention->reticleSpinCounter =
                (u8)((attention->reticleSpinCounter + 3u) |
                     0x80u);
            if (attention->reticleMotionCounter != 0) {
                attention->reticleMotionCounter =
                    (u8)(attention->reticleMotionCounter + 3u);
            }
            attention->previousReticleRadius =
                attention->reticleRadius;
            attention->reticleRadius =
                OwnerActor_ReadF32(constants, 0x0C);
        } else {
            float step =
                (OwnerActor_ReadF32(constants, 0x04) -
                 attention->reticleRadius) *
                OwnerActor_ReadF32(constants, 0x24);
            if (step < 30.0f) {
                step = 30.0f;
            } else if (step > 100.0f) {
                step = 100.0f;
            }
            attention->previousReticleRadius =
                attention->reticleRadius;
            if (
                Math_StepToF(
                    &attention->reticleRadius,
                    OwnerActor_ReadF32(constants, 0x08),
                    step
                )
            ) {
                ++attention->reticleSpinCounter;
            }
        }
    } else {
        attention->reticleActor = 0;
        attention->previousReticleRadius =
            attention->reticleRadius;
        Math_StepToF(
            &attention->reticleRadius,
            OwnerActor_ReadF32(constants, 0x04),
            OwnerActor_ReadF32(constants, 0x28)
        );
    }
}

void FUN_0047976c(
    Oot3dAttentionRuntime* attention,
    Oot3dPlayer* player,
    Oot3dActor* focusActor,
    Oot3dPlayState* play
) {
    Attention_Update(attention, player, focusActor, play);
}

static s32 OwnerActor_ObjectLoaded(
    Oot3dPlayState* play,
    const Oot3dActor* actor
) {
    return oot3d_field_80x4_positive(
        (u8*)play + OOT3D_OWNER_PLAY_OBJECT_CONTEXT,
        (s8)actor->objectSlot
    ) != 0;
}

static s32 OwnerActor_CanRunDeferredInit(u64 frameStart) {
    const u64 now = OwnerActor_SystemTick();
    const u64 elapsed =
        now >= frameStart ? now - frameStart : 0;
    return (elapsed * 3u) / 1000000u < 16u;
}

static void OwnerActor_CopyDynaTransforms(
    Oot3dPlayState* play
) {
    u8* dyna =
        (u8*)play + OOT3D_OWNER_PLAY_DYNA_CONTEXT;
    u32 index;

    for (index = 0; index < OOT3D_OWNER_DYNA_COUNT; ++index) {
        u8* entry =
            dyna + index * OOT3D_OWNER_DYNA_STRIDE;
        if (
            (
                OwnerActor_ReadU16(
                    dyna,
                    OOT3D_OWNER_DYNA_FLAGS + index * 2u
                ) & 1u
            ) != 0
        ) {
            memcpy(entry + 0x18, entry + 0x38, 0x20);
        }
    }
}

extern void FUN_00477e44(
    Oot3dPlayState* play,
    void* dynaContext
);

void Actor_UpdateAll(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext
) {
    Oot3dPlayer* player = OwnerActor_GetPlayer(play);
    const u32* freezeMasks =
        (const u32*)OwnerActor_ResolveGlobal(
            OOT3D_OWNER_FREEZE_MASKS
        );
    const u64 frameStart = OwnerActor_SystemTick();
    u32 freezeException = 0;
    Oot3dActor* talkActor = NULL;
    u32 category;

    if (
        (gOot3dRendererInitGuard & 1u) == 0 &&
        oot3d_static_init_guard_acquire(
            &gOot3dRendererInitGuard
        ) != 0
    ) {
        RendererGlobalState_Init(gOot3dRendererGlobalState);
    }
    FUN_0047ccdc(
        (Oot3dActorUpdateHalfwordRecord*)
            OwnerActor_ResolveGlobal(
                OOT3D_OWNER_ACTOR_UPDATE_RECORD
            )
    );
    Actor_SpawnSceneEntries(play);
    if (OwnerActor_ReadU8(actorContext, 2) != 0) {
        OwnerActor_WriteU8(
            actorContext, 2,
            (u8)(OwnerActor_ReadU8(actorContext, 2) - 1u)
        );
    }
    if (
        (
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_STATE_FLAGS2
            ) & 0x08000000u
        ) != 0
    ) {
        freezeException = 0x02000000u;
    }
    if (
        (
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_STATE_FLAGS1
            ) & 0x40u
        ) != 0 &&
        (
            OwnerActor_ReadU16(player, 0x116) & 0xFF00u
        ) != 0x0600u
    ) {
        talkActor = OwnerActor_DecodeActor(
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_TALK_ACTOR
            )
        );
    }

    for (category = 0; category < 12; ++category) {
        const u32 canFreeze =
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_STATE_FLAGS1
            ) & freezeMasks[category];
        Oot3dActor* actor = OwnerActor_DecodeActor(
            actorContext->lists[category].head
        );

        while (actor != NULL) {
            Oot3dActor* next = OwnerActor_DecodeActor(actor->next);
            const s32 objectLoaded =
                OwnerActor_ObjectLoaded(play, actor);

            if (actor->worldPos.y < -25000.0f) {
                actor->worldPos.y = -25000.0f;
            }
            OwnerActor_WriteU32(actor, 0x24, 0);

            if (actor->init != 0) {
                if (
                    objectLoaded &&
                    OwnerActor_CanRunDeferredInit(frameStart)
                ) {
                    Oot3dActorCallback init =
                        OwnerActor_ResolveCallback(actor->init);
                    init(actor, play);
                    actor->init = 0;
                }
            } else if (!objectLoaded) {
                Actor_Kill(actor);
            } else if (
                (
                    freezeException != 0 &&
                    (actor->flags & freezeException) == 0
                ) ||
                (
                    freezeException == 0 &&
                    canFreeze != 0 &&
                    actor != talkActor &&
                    actor != OwnerActor_DecodeActor(
                        OwnerActor_ReadU32(
                            player,
                            OOT3D_OWNER_PLAYER_NAVI_ACTOR
                        )
                    ) &&
                    actor != OwnerActor_DecodeActor(
                        OwnerActor_ReadU32(
                            player,
                            OOT3D_OWNER_PLAYER_HELD_ACTOR
                        )
                    ) &&
                    OwnerActor_DecodeActor(actor->parent) !=
                        (Oot3dActor*)player
                )
            ) {
                FUN_0047c938(
                    (Oot3dActorUpdateDefaultsRecord*)
                        &actor->colChkInfo
                );
            } else if (actor->update == 0) {
                if (actor->isDrawn == 0) {
                    next = Actor_Delete(
                        actorContext, actor, play
                    );
                } else {
                    Actor_Destroy(actor, play);
                }
            } else {
                Oot3dActorCallback update;
                const float dx =
                    ((Oot3dActor*)player)->worldPos.x -
                    actor->worldPos.x;
                const float dz =
                    ((Oot3dActor*)player)->worldPos.z -
                    actor->worldPos.z;

                actor->prevPos = actor->worldPos;
                actor->xzDistToPlayer =
                    sqrtf(dx * dx + dz * dz);
                actor->yDistToPlayer =
                    ((Oot3dActor*)player)->worldPos.y -
                    actor->worldPos.y;
                actor->xyzDistToPlayerSq =
                    actor->xzDistToPlayer *
                        actor->xzDistToPlayer +
                    actor->yDistToPlayer *
                        actor->yDistToPlayer;
                actor->yawTowardsPlayer =
                    Math_Atan2S(dx, dz);
                actor->flags &= ~0x01000000u;

                if (
                    (
                        actor->freezeTimer == 0 ||
                        --actor->freezeTimer == 0
                    ) &&
                    (actor->flags & 0x50u) != 0
                ) {
                    actor->isLockedOn =
                        OwnerActor_ReadU32(
                            player,
                            OOT3D_OWNER_PLAYER_FOCUS_ACTOR
                        ) == OwnerActor_EncodePointer(actor);
                    if (
                        actor->attentionPriority != 0 &&
                        OwnerActor_ReadU32(
                            player,
                            OOT3D_OWNER_PLAYER_FOCUS_ACTOR
                        ) == 0
                    ) {
                        actor->attentionPriority = 0;
                    }
                    if (actor->colorFilterTimer > 0) {
                        --actor->colorFilterTimer;
                    }
                    if (actor->updateTimer > 0) {
                        --actor->updateTimer;
                    }
                    update =
                        OwnerActor_ResolveCallback(actor->update);
                    update(actor, play);
                    FUN_0047af24(
                        play,
                        (u8*)play +
                            OOT3D_OWNER_PLAY_DYNA_CONTEXT,
                        actor
                    );
                }
                FUN_0047c938(
                    (Oot3dActorUpdateDefaultsRecord*)
                        &actor->colChkInfo
                );
            }
            actor = next;
        }
        if (category == 1) {
            FUN_00477e44(
                play,
                (u8*)play + OOT3D_OWNER_PLAY_DYNA_CONTEXT
            );
        }
    }

    {
        Oot3dActor* focus = OwnerActor_DecodeActor(
            OwnerActor_ReadU32(
                player, OOT3D_OWNER_PLAYER_FOCUS_ACTOR
            )
        );
        Oot3dAttentionRuntime* attention =
            (Oot3dAttentionRuntime*)((u8*)actorContext +
                OOT3D_OWNER_ATTENTION_OFFSET);

        if (focus != NULL && focus->update == 0) {
            focus = NULL;
            FUN_00334354(player);
        }
        if (
            focus == NULL ||
            OwnerActor_ReadS32(
                player, OOT3D_OWNER_PLAYER_TARGET_TIMER
            ) <= 7
        ) {
            if (attention->reticleSpinCounter != 0) {
                attention->reticleSpinCounter = 0;
                Audio_PlaySoundGeneral(
                    0x01000494u,
                    NULL,
                    4,
                    (float*)OwnerActor_ResolveGlobal(
                        OOT3D_OWNER_AUDIO_SCALE
                    ),
                    (float*)OwnerActor_ResolveGlobal(
                        OOT3D_OWNER_AUDIO_SCALE
                    ),
                    (s8*)OwnerActor_ResolveGlobal(
                        OOT3D_OWNER_AUDIO_REVERB
                    )
                );
            }
            focus = NULL;
        }
        Attention_Update(attention, player, focus, play);
    }
    FUN_0047955c(
        (u32)(uintptr_t)play,
        (Oot3dPauseUiDualAlphaState*)((u8*)actorContext +
            OOT3D_OWNER_TITLE_CARD_OFFSET)
    );
    OwnerActor_CopyDynaTransforms(play);
}

void FUN_00461344(
    Oot3dPlayState* play,
    Oot3dActorContext* actorContext
) {
    Actor_UpdateAll(play, actorContext);
}
