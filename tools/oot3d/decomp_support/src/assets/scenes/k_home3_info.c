/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: k_home3_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_k_home3_info_k_home3_0_info_actors[] = {
    { ACTOR_EN_KO, { -43, 0, 76 }, { 0, -24030, 0 }, -255 },
    { ACTOR_EN_KO, { -121, 16, 89 }, { 0, 23484, 0 }, -247 },
    { ACTOR_OBJ_TSUBO, { 33, 0, -55 }, { 0, 0, 0 }, 17152 },
    { ACTOR_OBJ_TSUBO, { 35, 0, 57 }, { 0, 0, 0 }, 17665 },
};

static const Oot3dRoomObjectEntry oot3d_k_home3_info_k_home3_0_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_k_home3_info_room_refs[] = {
    { "k_home3_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_k_home3_info_rooms[] = {
    { "k_home3_0_info.zsi", 0, oot3d_k_home3_info_k_home3_0_info_objects, 4u, oot3d_k_home3_info_k_home3_0_info_actors, 4u },
};

const Oot3dSceneIndex oot3d_scene_index_k_home3_info = {
    "k_home3_info.zsi",
    oot3d_k_home3_info_room_refs, 1u,
    oot3d_k_home3_info_rooms, 1u,
    NULL, 0u,
};
