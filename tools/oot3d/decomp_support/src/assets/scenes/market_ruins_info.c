/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: market_ruins_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_market_ruins_info_market_ruins_0_info_actors[] = {
    { ACTOR_EN_RD, { 425, 0, 203 }, { 0, -20388, 0 }, -32512 },
    { ACTOR_EN_RD, { 200, 0, 316 }, { 0, -21844, 0 }, -32512 },
    { ACTOR_EN_RD, { -199, 0, 268 }, { 0, 21663, 0 }, -32512 },
    { ACTOR_EN_RD, { -239, 0, 505 }, { 0, 18568, 0 }, -32510 },
    { ACTOR_EN_RD, { 247, 0, -460 }, { 0, -4733, 0 }, -32512 },
    { ACTOR_EN_RD, { -283, 0, -154 }, { 0, 10193, 0 }, -32510 },
    { ACTOR_EN_RD, { 223, 0, 17 }, { 0, -15474, 0 }, -32510 },
    { ACTOR_EN_RD, { -170, 0, -425 }, { 0, 4914, 0 }, -32512 },
    { ACTOR_BG_SPOT16_DOUGHNUT, { 10840, 6200, -9600 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_market_ruins_info_market_ruins_0_info_objects[] = {
    { OBJECT_HORSE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_DOUGHNUT, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_market_ruins_info_room_refs[] = {
    { "market_ruins_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_market_ruins_info_rooms[] = {
    { "market_ruins_0_info.zsi", 0, oot3d_market_ruins_info_market_ruins_0_info_objects, 3u, oot3d_market_ruins_info_market_ruins_0_info_actors, 9u },
};

const Oot3dSceneIndex oot3d_scene_index_market_ruins_info = {
    "market_ruins_info.zsi",
    oot3d_market_ruins_info_room_refs, 1u,
    oot3d_market_ruins_info_rooms, 1u,
    NULL, 0u,
};
