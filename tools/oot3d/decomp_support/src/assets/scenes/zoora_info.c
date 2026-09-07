/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: zoora_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_zoora_info_zoora_0_info_actors[] = {
    { ACTOR_EN_ZO, { 75, 0, -69 }, { 0, -8555, 0 }, -56 },
    { ACTOR_EN_OSSAN, { 0, 0, -180 }, { 0, 0, 0 }, 7 },
    { ACTOR_EN_TANA, { 0, 0, -140 }, { 0, 0, 0 }, 2 },
};

static const Oot3dRoomObjectEntry oot3d_zoora_info_zoora_0_info_objects[] = {
    { OBJECT_OSSAN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_NUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_ARROW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_LIQUID, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_FISH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_CLOTHES, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MASTERZOORA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZO, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_zoora_info_room_refs[] = {
    { "zoora_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_zoora_info_rooms[] = {
    { "zoora_0_info.zsi", 0, oot3d_zoora_info_zoora_0_info_objects, 10u, oot3d_zoora_info_zoora_0_info_actors, 3u },
};

const Oot3dSceneIndex oot3d_scene_index_zoora_info = {
    "zoora_info.zsi",
    oot3d_zoora_info_room_refs, 1u,
    oot3d_zoora_info_rooms, 1u,
    NULL, 0u,
};
