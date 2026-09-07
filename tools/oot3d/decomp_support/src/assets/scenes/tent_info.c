/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: tent_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_tent_info_tent_0_info_actors[] = {
    { ACTOR_EN_DAIKU, { 101, 0, 16 }, { 0, -16930, 0 }, -16 },
    { ACTOR_EN_DAIKU, { 103, 0, 56 }, { 0, -20571, 0 }, -15 },
    { ACTOR_EN_DAIKU, { 100, 0, 101 }, { 0, -22027, 0 }, -14 },
    { ACTOR_EN_DAIKU, { 95, 0, 143 }, { 0, -23302, 0 }, -13 },
    { ACTOR_EN_MM2, { -75, 0, -135 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_RIVER_SOUND, { -1, 0, 4 }, { 0, 0, 0 }, 20 },
};

static const Oot3dRoomObjectEntry oot3d_tent_info_tent_0_info_objects[] = {
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DAIKU, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_MM, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_tent_info_room_refs[] = {
    { "tent_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_tent_info_rooms[] = {
    { "tent_0_info.zsi", 0, oot3d_tent_info_tent_0_info_objects, 3u, oot3d_tent_info_tent_0_info_actors, 6u },
};

const Oot3dSceneIndex oot3d_scene_index_tent_info = {
    "tent_info.zsi",
    oot3d_tent_info_room_refs, 1u,
    oot3d_tent_info_rooms, 1u,
    NULL, 0u,
};
