/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: stable_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_stable_info_stable_0_info_actors[] = {
    { ACTOR_EN_IN, { -35, 0, 95 }, { 0, 30948, 2 }, -1 },
    { ACTOR_EN_IN, { -35, 0, 95 }, { 0, 30948, 3 }, -1 },
    { ACTOR_EN_IN, { -35, 0, 95 }, { 0, 30948, 4 }, -1 },
    { ACTOR_EN_MA2, { 120, 0, 0 }, { 0, -13653, 5 }, -1 },
    { ACTOR_EN_HORSE_NORMAL, { 355, 0, -245 }, { 0, 0, 170 }, 15 },
    { ACTOR_EN_COW, { -122, 0, -257 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_COW, { -3, 0, -257 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_HORSE_NORMAL, { 238, 0, -245 }, { 0, 0, 170 }, 15 },
    { ACTOR_EN_TA, { -149, 0, 2 }, { 0, 16384, 0 }, 2 },
};

static const Oot3dRoomObjectEntry oot3d_stable_info_stable_0_info_objects[] = {
    { OBJECT_HORSE_NORMAL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MA2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_IN, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_stable_info_room_refs[] = {
    { "stable_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_stable_info_rooms[] = {
    { "stable_0_info.zsi", 0, oot3d_stable_info_stable_0_info_objects, 5u, oot3d_stable_info_stable_0_info_actors, 9u },
};

const Oot3dSceneIndex oot3d_scene_index_stable_info = {
    "stable_info.zsi",
    oot3d_stable_info_room_refs, 1u,
    oot3d_stable_info_rooms, 1u,
    NULL, 0u,
};
