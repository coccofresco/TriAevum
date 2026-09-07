/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shop_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shop_info_shop_0_info_actors[] = {
    { ACTOR_EN_GO2, { 54, 0, 73 }, { 0, -21117, 0 }, -19 },
    { ACTOR_EN_HY, { -124, 0, 77 }, { 0, 8555, 0 }, 1940 },
    { ACTOR_EN_TANA, { -30, 0, 0 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_OSSAN, { -30, 0, -40 }, { 0, 0, 0 }, 4 },
};

static const Oot3dRoomObjectEntry oot3d_shop_info_shop_0_info_objects[] = {
    { OBJECT_OSSAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_ARROW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_STICK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_NUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMB_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shop_info_room_refs[] = {
    { "shop_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shop_info_rooms[] = {
    { "shop_0_info.zsi", 0, oot3d_shop_info_shop_0_info_objects, 12u, oot3d_shop_info_shop_0_info_actors, 4u },
};

const Oot3dSceneIndex oot3d_scene_index_shop_info = {
    "shop_info.zsi",
    oot3d_shop_info_room_refs, 1u,
    oot3d_shop_info_rooms, 1u,
    NULL, 0u,
};
