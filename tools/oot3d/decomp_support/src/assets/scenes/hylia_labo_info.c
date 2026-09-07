/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hylia_labo_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_hylia_labo_info_hylia_labo_0_info_actors[] = {
    { ACTOR_EN_MK, { -100, 0, 53 }, { 0, 2001, 0 }, 0 },
    { ACTOR_EN_ITEM00, { 68, -372, -207 }, { 0, 0, 0 }, 258 },
    { ACTOR_EN_ITEM00, { -52, -372, -32 }, { 0, 0, 0 }, 514 },
    { ACTOR_EN_ITEM00, { -169, -372, -124 }, { 0, 0, 0 }, 770 },
    { ACTOR_OBJ_KIBAKO2, { 90, -372, -119 }, { 0, 0, 0 }, 29448 },
    { ACTOR_EN_RIVER_SOUND, { -173, 53, 256 }, { 0, 0, 0 }, 15 },
    { ACTOR_EN_RIVER_SOUND, { -164, 41, 76 }, { 0, 0, 0 }, 15 },
};

static const Oot3dRoomObjectEntry oot3d_hylia_labo_info_hylia_labo_0_info_objects[] = {
    { OBJECT_MK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ST, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_hylia_labo_info_room_refs[] = {
    { "hylia_labo_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_hylia_labo_info_rooms[] = {
    { "hylia_labo_0_info.zsi", 0, oot3d_hylia_labo_info_hylia_labo_0_info_objects, 4u, oot3d_hylia_labo_info_hylia_labo_0_info_actors, 7u },
};

const Oot3dSceneIndex oot3d_scene_index_hylia_labo_info = {
    "hylia_labo_info.zsi",
    oot3d_hylia_labo_info_room_refs, 1u,
    oot3d_hylia_labo_info_rooms, 1u,
    NULL, 0u,
};
