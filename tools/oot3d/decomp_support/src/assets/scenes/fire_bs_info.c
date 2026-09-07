/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: fire_bs_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_fire_bs_info_fire_bs_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -4, -100, 5 }, { 0, 0, 0 }, 7 },
    { ACTOR_BOSS_FD, { -2, 90, 11 }, { 0, 0, 0 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_fire_bs_info_fire_bs_1_info_objects[] = {
    { OBJECT_HIDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BDOOR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_FD2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_fire_bs_info_room_refs[] = {
    { "fire_bs_0_info.zsi", 0 },
    { "fire_bs_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_fire_bs_info_rooms[] = {
    { "fire_bs_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "fire_bs_1_info.zsi", 1, oot3d_fire_bs_info_fire_bs_1_info_objects, 8u, oot3d_fire_bs_info_fire_bs_1_info_actors, 2u },
};

const Oot3dSceneIndex oot3d_scene_index_fire_bs_info = {
    "fire_bs_info.zsi",
    oot3d_fire_bs_info_room_refs, 2u,
    oot3d_fire_bs_info_rooms, 2u,
    NULL, 0u,
};
