/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: kakariko_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_kakariko_info_kakariko_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 118, 0, -175 }, { 0, 0, 0 }, 20 },
    { ACTOR_EN_HY, { 134, 0, -123 }, { 0, 26761, 0 }, 1938 },
    { ACTOR_EN_TA, { -159, 16, -148 }, { 0, 16384, 0 }, 1 },
    { ACTOR_EN_TORYO, { 43, 0, -100 }, { 0, -9647, 0 }, -1 },
    { ACTOR_EN_DAIKU_KAKARIKO, { -122, 0, 115 }, { 0, 25121, 0 }, -255 },
    { ACTOR_EN_DAIKU_KAKARIKO, { -130, 0, 35 }, { 0, 14199, 0 }, -254 },
    { ACTOR_EN_DAIKU_KAKARIKO, { -146, 16, -124 }, { 0, 7099, 0 }, -253 },
    { ACTOR_EN_HY, { 132, 0, 53 }, { 0, -20571, 0 }, 1922 },
    { ACTOR_EN_HY, { -12, 0, 120 }, { 0, 24575, 0 }, 1929 },
    { ACTOR_EN_HY, { 24, 0, -137 }, { 0, -2001, 0 }, 1920 },
    { ACTOR_EN_HY, { -134, 0, -63 }, { 0, 0, 0 }, 7 },
};

static const Oot3dRoomObjectEntry oot3d_kakariko_info_kakariko_0_info_objects[] = {
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BBA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TORYO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DAIKU, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_kakariko_info_room_refs[] = {
    { "kakariko_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_kakariko_info_rooms[] = {
    { "kakariko_0_info.zsi", 0, oot3d_kakariko_info_kakariko_0_info_objects, 10u, oot3d_kakariko_info_kakariko_0_info_actors, 11u },
};

const Oot3dSceneIndex oot3d_scene_index_kakariko_info = {
    "kakariko_info.zsi",
    oot3d_kakariko_info_room_refs, 1u,
    oot3d_kakariko_info_rooms, 1u,
    NULL, 0u,
};
