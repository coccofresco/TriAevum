/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: labo_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_labo_info_labo_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -148, 0, 226 }, { 0, 0, 0 }, 20 },
    { ACTOR_EN_HY, { 105, 0, -4 }, { 0, -2366, 0 }, 1937 },
    { ACTOR_EN_NIW_LADY, { -99, 0, 117 }, { 0, 30948, 0 }, -1 },
    { ACTOR_EN_HY, { -74, 0, -194 }, { 0, 14745, 0 }, 1930 },
    { ACTOR_EN_ITEM00, { 86, 0, -200 }, { 0, 0, 0 }, 262 },
    { ACTOR_EN_COW, { 140, 0, -255 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_WONDER_ITEM, { 153, 91, -266 }, { 0, 0, 1 }, 4738 },
};

static const Oot3dRoomObjectEntry oot3d_labo_info_labo_0_info_objects[] = {
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_CNE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BBA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ANE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_labo_info_room_refs[] = {
    { "labo_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_labo_info_rooms[] = {
    { "labo_0_info.zsi", 0, oot3d_labo_info_labo_0_info_objects, 11u, oot3d_labo_info_labo_0_info_actors, 7u },
};

const Oot3dSceneIndex oot3d_scene_index_labo_info = {
    "labo_info.zsi",
    oot3d_labo_info_room_refs, 1u,
    oot3d_labo_info_rooms, 1u,
    NULL, 0u,
};
