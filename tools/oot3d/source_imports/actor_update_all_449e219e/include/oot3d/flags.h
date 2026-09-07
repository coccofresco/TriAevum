#ifndef OOT3D_FLAGS_H
#define OOT3D_FLAGS_H

#include "oot3d/types.h"

typedef struct Oot3dPlayState Oot3dPlayState;

enum {
    OOT3D_PLAY_SCENE_FLAGS_OFFSET = 0x2228,
    OOT3D_PLAY_ENV_FLAGS_OFFSET = 0x5F98,
    OOT3D_SAVE_EVENT_CHK_INF_OFFSET = 0x0EEC,
};

/*
 * Native scene-local flag block at PlayState+0x2228. The two-word switch and
 * collectible sets accept the 0..63 indices used by scene and actor data.
 */
typedef struct Oot3dSceneFlags {
    u32 switchFlags[2];
    u8 unk_08[0x08];
    u32 treasureFlags;
    u32 clearFlags;
    u32 tempClearFlags;
    u32 collectibleFlags[2];
} Oot3dSceneFlags;

#if defined(__cplusplus)
static_assert(sizeof(Oot3dSceneFlags) == 0x24, "SceneFlags size");
static_assert(
    offsetof(Oot3dSceneFlags, treasureFlags) == 0x10,
    "SceneFlags treasure offset"
);
static_assert(
    offsetof(Oot3dSceneFlags, collectibleFlags) == 0x1C,
    "SceneFlags collectible offset"
);
#else
_Static_assert(sizeof(Oot3dSceneFlags) == 0x24, "SceneFlags size");
_Static_assert(
    offsetof(Oot3dSceneFlags, treasureFlags) == 0x10,
    "SceneFlags treasure offset"
);
_Static_assert(
    offsetof(Oot3dSceneFlags, collectibleFlags) == 0x1C,
    "SceneFlags collectible offset"
);
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern u8 gOot3dSaveContext[];

u32 Flags_GetSwitch(const Oot3dPlayState* play, s32 flag);
void Flags_SetSwitch(Oot3dPlayState* play, s32 flag);
void Flags_UnsetSwitch(Oot3dPlayState* play, s32 flag);

u32 Flags_GetTreasure(const Oot3dPlayState* play, s32 flag);
u32 Flags_GetClear(const Oot3dPlayState* play, s32 flag);
void Flags_SetClear(Oot3dPlayState* play, s32 flag);
u32 Flags_GetTempClear(const Oot3dPlayState* play, s32 flag);
void Flags_SetTempClear(Oot3dPlayState* play, s32 flag);

u32 Flags_GetCollectible(const Oot3dPlayState* play, s32 flag);
void Flags_SetCollectible(Oot3dPlayState* play, s32 flag);

u16 Flags_GetEnv(const Oot3dPlayState* play, s32 flag);
void Flags_SetEnv(Oot3dPlayState* play, s32 flag);
void Flags_UnsetEnv(Oot3dPlayState* play, s32 flag);

u32 Flags_GetEventChkInf(s32 flag);
void Flags_SetEventChkInf(s32 flag);

#ifdef __cplusplus
}
#endif

#endif
