/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shop_alley_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shop_alley_info_shop_alley_0_info_actors[] = {
    { ACTOR_EN_HY, { -11, 0, 86 }, { 0, 16201, 0 }, 1939 },
    { ACTOR_EN_OSSAN, { 40, 0, -80 }, { 0, 0, 0 }, 3 },
    { ACTOR_EN_TANA, { 40, 0, -40 }, { 0, 0, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_shop_alley_info_shop_alley_0_info_objects[] = {
    { OBJECT_DS2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_LIQUID, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_FIRE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_INSECT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BUTTERFLY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_FISH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_GHOST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SOUL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_NUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shop_alley_info_room_refs[] = {
    { "shop_alley_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shop_alley_info_rooms[] = {
    { "shop_alley_0_info.zsi", 0, oot3d_shop_alley_info_shop_alley_0_info_objects, 13u, oot3d_shop_alley_info_shop_alley_0_info_actors, 3u },
};

const Oot3dSceneIndex oot3d_scene_index_shop_alley_info = {
    "shop_alley_info.zsi",
    oot3d_shop_alley_info_room_refs, 1u,
    oot3d_shop_alley_info_rooms, 1u,
    NULL, 0u,
};
