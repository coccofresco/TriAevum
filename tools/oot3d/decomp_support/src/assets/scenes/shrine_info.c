/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: shrine_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_shrine_info_shrine_0_info_actors[] = {
    { ACTOR_BG_SPOT16_DOUGHNUT, { 10320, 6600, -9600 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_GS, { 1000, 60, -680 }, { 0, -16384, 0 }, 14342 },
    { ACTOR_EN_GS, { 1000, 60, -615 }, { 0, -16384, 0 }, 14599 },
    { ACTOR_EN_GS, { 1000, 60, -550 }, { 0, -16384, 0 }, 14862 },
    { ACTOR_EN_GS, { 1000, 60, -485 }, { 0, -16384, 0 }, 15120 },
};

static const Oot3dRoomObjectEntry oot3d_shrine_info_shrine_0_info_objects[] = {
    { OBJECT_SD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_DOUGHNUT, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_shrine_info_room_refs[] = {
    { "shrine_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_shrine_info_rooms[] = {
    { "shrine_0_info.zsi", 0, oot3d_shrine_info_shrine_0_info_objects, 4u, oot3d_shrine_info_shrine_0_info_actors, 5u },
};

const Oot3dSceneIndex oot3d_scene_index_shrine_info = {
    "shrine_info.zsi",
    oot3d_shrine_info_room_refs, 1u,
    oot3d_shrine_info_rooms, 1u,
    NULL, 0u,
};
