/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: market_alley_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_market_alley_info_market_alley_0_info_actors[] = {
    { ACTOR_EN_HY, { -212, 0, 1353 }, { 0, -32767, 0 }, 1934 },
    { ACTOR_EN_HY, { 456, 0, 712 }, { 0, 16384, 0 }, 1935 },
    { ACTOR_EN_HEISHI4, { -328, 0, 1199 }, { 0, 0, 0 }, -249 },
    { ACTOR_EN_HY, { -574, 0, 1053 }, { 0, -32767, 0 }, 1933 },
    { ACTOR_EN_RIVER_SOUND, { 515, -4, 759 }, { 0, 0, 0 }, 11 },
};

static const Oot3dRoomObjectEntry oot3d_market_alley_info_market_alley_0_info_objects[] = {
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_NIW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_CNE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_market_alley_info_room_refs[] = {
    { "market_alley_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_market_alley_info_rooms[] = {
    { "market_alley_0_info.zsi", 0, oot3d_market_alley_info_market_alley_0_info_objects, 11u, oot3d_market_alley_info_market_alley_0_info_actors, 5u },
};

const Oot3dSceneIndex oot3d_scene_index_market_alley_info = {
    "market_alley_info.zsi",
    oot3d_market_alley_info_room_refs, 1u,
    oot3d_market_alley_info_rooms, 1u,
    NULL, 0u,
};
