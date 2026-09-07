/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: market_night_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_market_night_info_market_night_0_info_actors[] = {
    { ACTOR_EN_HEISHI4, { -106, 0, -510 }, { 0, 0, 0 }, -248 },
    { ACTOR_EN_HEISHI4, { 111, 0, -510 }, { 0, 0, 0 }, -248 },
    { ACTOR_EN_TG, { 136, 0, -18 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_MA1, { 0, 0, -150 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_WOOD02, { -100, 0, 240 }, { 0, 0, 0 }, -254 },
    { ACTOR_EN_WONDER_ITEM, { -524, 180, -246 }, { 0, 0, 1 }, 4673 },
    { ACTOR_EN_WONDER_ITEM, { -521, 180, 27 }, { 0, 0, 1 }, 4674 },
    { ACTOR_EN_KUSA, { 76, 20, -190 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 75, 20, -310 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 73, 20, -428 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 432, 0, -548 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 465, 0, -548 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { 504, 0, -546 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -87, 0, 205 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_KUSA, { -74, 0, 233 }, { 0, 0, 0 }, 512 },
    { ACTOR_EN_DOG, { 87, 0, 116 }, { 0, -8191, 0 }, 513 },
    { ACTOR_EN_DOG, { -125, 0, 159 }, { 0, -14928, 0 }, 785 },
    { ACTOR_EN_DOG, { 315, 0, 297 }, { 0, -5278, 0 }, 1057 },
    { ACTOR_EN_DOG, { -131, 0, 421 }, { 0, -22209, 0 }, 1329 },
    { ACTOR_EN_DOG, { 257, 0, -268 }, { 0, -11105, 0 }, 1857 },
    { ACTOR_EN_DOG, { 423, 0, 421 }, { 0, 5460, 0 }, 80 },
    { ACTOR_EN_DOG, { -199, 0, -257 }, { 0, -22755, 0 }, 352 },
};

static const Oot3dRoomObjectEntry oot3d_market_night_info_market_night_0_info_objects[] = {
    { OBJECT_SD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WOOD02, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MA1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_DOUGHNUT, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_market_night_info_room_refs[] = {
    { "market_night_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_market_night_info_rooms[] = {
    { "market_night_0_info.zsi", 0, oot3d_market_night_info_market_night_0_info_objects, 9u, oot3d_market_night_info_market_night_0_info_actors, 22u },
};

const Oot3dSceneIndex oot3d_scene_index_market_night_info = {
    "market_night_info.zsi",
    oot3d_market_night_info_room_refs, 1u,
    oot3d_market_night_info_rooms, 1u,
    NULL, 0u,
};
