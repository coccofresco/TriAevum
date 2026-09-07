/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: kokiri_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_kokiri_info_kokiri_0_info_actors[] = {
    { ACTOR_EN_KO, { -114, 0, 64 }, { 0, 9102, 0 }, -246 },
    { ACTOR_EN_KO, { 89, 16, 27 }, { 0, -9466, 0 }, -251 },
    { ACTOR_EN_OSSAN, { 0, 0, -59 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_TANA, { 0, 0, -20 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_WONDER_ITEM, { 140, 0, -110 }, { 0, 0, 1 }, 4688 },
};

static const Oot3dRoomObjectEntry oot3d_kokiri_info_kokiri_0_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_ARROW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SHIELD_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_NUTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_STICK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SHOP_DUNGEN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_SEED, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MASTERKOKIRI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MASTERKOKIRIHEAD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_kokiri_info_room_refs[] = {
    { "kokiri_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_kokiri_info_rooms[] = {
    { "kokiri_0_info.zsi", 0, oot3d_kokiri_info_kokiri_0_info_objects, 12u, oot3d_kokiri_info_kokiri_0_info_actors, 5u },
};

const Oot3dSceneIndex oot3d_scene_index_kokiri_info = {
    "kokiri_info.zsi",
    oot3d_kokiri_info_room_refs, 1u,
    oot3d_kokiri_info_rooms, 1u,
    NULL, 0u,
};
