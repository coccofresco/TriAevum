/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: kinsuta_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_kinsuta_info_kinsuta_0_info_actors[] = {
    { ACTOR_EN_SSH, { 0, 280, 20 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_STH, { 0, 0, 20 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_SSH, { 240, 280, 140 }, { 0, -21844, 0 }, 1 },
    { ACTOR_EN_STH, { 240, 0, 140 }, { 0, -21844, 0 }, 1 },
    { ACTOR_EN_SSH, { -240, 280, 140 }, { 0, 21844, 0 }, 2 },
    { ACTOR_EN_STH, { -240, 0, 140 }, { 0, 21844, 0 }, 2 },
    { ACTOR_EN_SSH, { 0, 280, -270 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_STH, { 0, 0, -270 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_SSH, { -240, 280, -140 }, { 0, 10922, 0 }, 4 },
    { ACTOR_EN_STH, { -240, 0, -140 }, { 0, 10922, 0 }, 4 },
    { ACTOR_EN_SSH, { 240, 280, -140 }, { 0, -5460, 0 }, 5 },
    { ACTOR_EN_STH, { 240, 0, -140 }, { 0, -5460, 0 }, 5 },
};

static const Oot3dRoomObjectEntry oot3d_kinsuta_info_kinsuta_0_info_objects[] = {
    { OBJECT_SSH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_O_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OE_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_kinsuta_info_room_refs[] = {
    { "kinsuta_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_kinsuta_info_rooms[] = {
    { "kinsuta_0_info.zsi", 0, oot3d_kinsuta_info_kinsuta_0_info_objects, 7u, oot3d_kinsuta_info_kinsuta_0_info_actors, 12u },
};

const Oot3dSceneIndex oot3d_scene_index_kinsuta_info = {
    "kinsuta_info.zsi",
    oot3d_kinsuta_info_room_refs, 1u,
    oot3d_kinsuta_info_rooms, 1u,
    NULL, 0u,
};
