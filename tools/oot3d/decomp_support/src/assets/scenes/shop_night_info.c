/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shop_night_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shop_night_info_shop_night_0_info_actors[] = {
    { ACTOR_EN_OSSAN, { 0, 0, -40 }, { 0, 0, 0 }, 2 },
    { ACTOR_EN_TANA, { 0, 0, 0 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_shop_night_info_shop_night_0_info_objects[] = {
    { OBJECT_RS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SOLDOUT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMB_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shop_night_info_room_refs[] = {
    { "shop_night_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shop_night_info_rooms[] = {
    { "shop_night_0_info.zsi", 0, oot3d_shop_night_info_shop_night_0_info_objects, 4u, oot3d_shop_night_info_shop_night_0_info_actors, 2u },
};

const Oot3dSceneIndex oot3d_scene_index_shop_night_info = {
    "shop_night_info.zsi",
    oot3d_shop_night_info_room_refs, 1u,
    oot3d_shop_night_info_rooms, 1u,
    NULL, 0u,
};
