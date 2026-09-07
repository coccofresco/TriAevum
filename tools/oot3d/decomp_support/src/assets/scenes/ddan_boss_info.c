/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: ddan_boss_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_ddan_boss_info_ddan_boss_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -895, -1524, -3312 }, { 0, 0, 0 }, 6 },
    { ACTOR_BOSS_DODONGO, { -1386, -1504, -3339 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -1422, -1504, -3821 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -1416, -1504, -2769 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -370, -1504, -3827 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOMBF, { -352, -1504, -2781 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOX, { -1109, -744, -2786 }, { 0, -16384, 0 }, 20512 },
    { ACTOR_BG_BREAKWALL, { -890, -744, -2784 }, { 0, 0, 0 }, 16385 },
};

static const Oot3dRoomObjectEntry oot3d_ddan_boss_info_ddan_boss_1_info_objects[] = {
    { OBJECT_DDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KINGDODONGO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOMBF, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_ddan_boss_info_room_refs[] = {
    { "ddan_boss_0_info.zsi", 0 },
    { "ddan_boss_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_ddan_boss_info_rooms[] = {
    { "ddan_boss_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "ddan_boss_1_info.zsi", 1, oot3d_ddan_boss_info_ddan_boss_1_info_objects, 6u, oot3d_ddan_boss_info_ddan_boss_1_info_actors, 8u },
};

const Oot3dSceneIndex oot3d_scene_index_ddan_boss_info = {
    "ddan_boss_info.zsi",
    oot3d_ddan_boss_info_room_refs, 2u,
    oot3d_ddan_boss_info_rooms, 2u,
    NULL, 0u,
};
