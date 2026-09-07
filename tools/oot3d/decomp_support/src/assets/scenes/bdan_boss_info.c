/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: bdan_boss_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_bdan_boss_info_bdan_boss_1_info_actors[] = {
    { ACTOR_BOSS_VA, { 10, 0, -220 }, { 0, 0, 0 }, -1 },
    { ACTOR_OBJ_TSUBO, { 279, 10, -761 }, { 0, 0, 0 }, 16387 },
    { ACTOR_OBJ_TSUBO, { -268, 10, -786 }, { 0, 0, 0 }, 16899 },
    { ACTOR_OBJ_TSUBO, { -543, 10, -496 }, { 0, 0, 0 }, 17411 },
    { ACTOR_OBJ_TSUBO, { 554, 10, -493 }, { 0, 0, 0 }, 17923 },
    { ACTOR_OBJ_TSUBO, { 551, 10, 36 }, { 0, 0, 0 }, 18435 },
    { ACTOR_OBJ_TSUBO, { -551, 10, 33 }, { 0, 0, 0 }, 18947 },
};

static const Oot3dRoomObjectEntry oot3d_bdan_boss_info_bdan_boss_1_info_objects[] = {
    { OBJECT_BDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RU1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BV, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BUBBLE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_bdan_boss_info_room_refs[] = {
    { "bdan_boss_0_info.zsi", 0 },
    { "bdan_boss_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_bdan_boss_info_rooms[] = {
    { "bdan_boss_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "bdan_boss_1_info.zsi", 1, oot3d_bdan_boss_info_bdan_boss_1_info_objects, 8u, oot3d_bdan_boss_info_bdan_boss_1_info_actors, 7u },
};

const Oot3dSceneIndex oot3d_scene_index_bdan_boss_info = {
    "bdan_boss_info.zsi",
    oot3d_bdan_boss_info_room_refs, 2u,
    oot3d_bdan_boss_info_rooms, 2u,
    NULL, 0u,
};
