/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: kakariko_home3_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_kakariko_home3_info_kakariko_home3_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 95, 0, -118 }, { 0, 0, 0 }, 20 },
    { ACTOR_EN_HY, { 17, 0, -83 }, { 0, -8374, 0 }, 1934 },
    { ACTOR_OBJ_TSUBO, { -146, 0, -97 }, { 0, 0, 0 }, 17155 },
    { ACTOR_OBJ_TSUBO, { -76, 0, -202 }, { 0, 0, 0 }, 17667 },
    { ACTOR_OBJ_TSUBO, { 97, 0, -203 }, { 0, 0, 0 }, 18191 },
};

static const Oot3dRoomObjectEntry oot3d_kakariko_home3_info_kakariko_home3_0_info_objects[] = {
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_kakariko_home3_info_room_refs[] = {
    { "kakariko_home3_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_kakariko_home3_info_rooms[] = {
    { "kakariko_home3_0_info.zsi", 0, oot3d_kakariko_home3_info_kakariko_home3_0_info_objects, 6u, oot3d_kakariko_home3_info_kakariko_home3_0_info_actors, 5u },
};

const Oot3dSceneIndex oot3d_scene_index_kakariko_home3_info = {
    "kakariko_home3_info.zsi",
    oot3d_kakariko_home3_info_room_refs, 1u,
    oot3d_kakariko_home3_info_rooms, 1u,
    NULL, 0u,
};
