/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: k_home_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_k_home_info_k_home_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 0, 0, 4 }, { 0, 0, 0 }, 20 },
    { ACTOR_EN_KO, { 64, 1, -108 }, { 0, -4915, 0 }, -249 },
    { ACTOR_EN_KO, { 1, 12, 121 }, { 0, -24576, 0 }, -248 },
    { ACTOR_EN_KO, { 92, 12, 79 }, { 0, -23120, 0 }, -245 },
    { ACTOR_EN_KO, { 14, 15, -36 }, { 0, 27125, 1 }, -249 },
    { ACTOR_EN_KO, { 85, 1, -84 }, { 0, -3095, 2 }, -249 },
    { ACTOR_OBJ_TSUBO, { -134, 0, -29 }, { 0, 0, 0 }, 16640 },
    { ACTOR_OBJ_TSUBO, { -68, 0, 114 }, { 0, 0, 0 }, 17152 },
};

static const Oot3dRoomObjectEntry oot3d_k_home_info_k_home_0_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_k_home_info_room_refs[] = {
    { "k_home_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_k_home_info_rooms[] = {
    { "k_home_0_info.zsi", 0, oot3d_k_home_info_k_home_0_info_objects, 4u, oot3d_k_home_info_k_home_0_info_actors, 8u },
};

const Oot3dSceneIndex oot3d_scene_index_k_home_info = {
    "k_home_info.zsi",
    oot3d_k_home_info_room_refs, 1u,
    oot3d_k_home_info_rooms, 1u,
    NULL, 0u,
};
