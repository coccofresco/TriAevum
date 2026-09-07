/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shop_golon_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shop_golon_info_shop_golon_0_info_actors[] = {
    { ACTOR_EN_OSSAN, { 0, 0, -180 }, { 0, 0, 0 }, 8 },
    { ACTOR_EN_TANA, { 0, 0, -140 }, { 0, 0, 0 }, 1 },
};

static const Oot3dRoomObjectEntry oot3d_shop_golon_info_shop_golon_0_info_objects[] = {
    { OBJECT_OF1D_MAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_STICK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMB_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_LIQUID, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_CLOTHES, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_LONGSWORD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MASTERGOLON, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SPOT18_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shop_golon_info_room_refs[] = {
    { "shop_golon_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shop_golon_info_rooms[] = {
    { "shop_golon_0_info.zsi", 0, oot3d_shop_golon_info_shop_golon_0_info_objects, 10u, oot3d_shop_golon_info_shop_golon_0_info_actors, 2u },
};

const Oot3dSceneIndex oot3d_scene_index_shop_golon_info = {
    "shop_golon_info.zsi",
    oot3d_shop_golon_info_room_refs, 1u,
    oot3d_shop_golon_info_rooms, 1u,
    NULL, 0u,
};
