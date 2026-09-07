/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: k_home5_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_k_home5_info_k_home5_0_info_actors[] = {
    { ACTOR_EN_KO, { 2, 1, -4 }, { 0, -16384, 0 }, -250 },
    { ACTOR_EN_SA, { 2, 0, -4 }, { 0, -16384, 0 }, 0 },
    { ACTOR_EN_ITEM00, { 45, 1, -52 }, { 0, 0, 0 }, 14339 },
    { ACTOR_EN_ITEM00, { 46, 1, 56 }, { 0, 0, 0 }, 14595 },
    { ACTOR_EN_ITEM00, { -46, 1, 57 }, { 0, 0, 0 }, 15107 },
    { ACTOR_EN_ITEM00, { -46, 1, -51 }, { 0, 0, 0 }, 14851 },
};

static const Oot3dRoomObjectEntry oot3d_k_home5_info_k_home5_0_info_objects[] = {
    { OBJECT_KM1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KW1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEART, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_k_home5_info_room_refs[] = {
    { "k_home5_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_k_home5_info_rooms[] = {
    { "k_home5_0_info.zsi", 0, oot3d_k_home5_info_k_home5_0_info_objects, 5u, oot3d_k_home5_info_k_home5_0_info_actors, 6u },
};

const Oot3dSceneIndex oot3d_scene_index_k_home5_info = {
    "k_home5_info.zsi",
    oot3d_k_home5_info_room_refs, 1u,
    oot3d_k_home5_info_rooms, 1u,
    NULL, 0u,
};
