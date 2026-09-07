/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: k_home4_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_k_home4_info_k_home4_0_info_actors[] = {
    { ACTOR_EN_KO, { -56, 16, 128 }, { 0, -27125, 0 }, -252 },
    { ACTOR_EN_MD, { 3, 48, 134 }, { 0, -32767, 0 }, -256 },
    { ACTOR_EN_KO, { -125, 12, 72 }, { 0, 21663, 0 }, -256 },
    { ACTOR_EN_BOX, { 58, 0, -55 }, { 0, 16384, 0 }, 22944 },
    { ACTOR_EN_BOX, { 58, 0, 35 }, { 0, 16384, 0 }, 22945 },
    { ACTOR_EN_BOX, { -60, 0, -55 }, { 0, -16384, 0 }, 22914 },
    { ACTOR_EN_BOX, { -60, 0, 35 }, { 0, -16384, 0 }, 22787 },
};

static const Oot3dRoomObjectEntry oot3d_k_home4_info_k_home4_0_info_objects[] = {
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_k_home4_info_room_refs[] = {
    { "k_home4_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_k_home4_info_rooms[] = {
    { "k_home4_0_info.zsi", 0, oot3d_k_home4_info_k_home4_0_info_objects, 5u, oot3d_k_home4_info_k_home4_0_info_actors, 7u },
};

const Oot3dSceneIndex oot3d_scene_index_k_home4_info = {
    "k_home4_info.zsi",
    oot3d_k_home4_info_room_refs, 1u,
    oot3d_k_home4_info_rooms, 1u,
    NULL, 0u,
};
