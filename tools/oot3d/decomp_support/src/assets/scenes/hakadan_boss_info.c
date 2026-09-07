/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hakadan_boss_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_hakadan_boss_info_hakadan_boss_0_info_actors[] = {
    { ACTOR_EN_LIGHT, { -110, 1721, -998 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 10, 1721, -998 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 10, 1721, -837 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { -110, 1721, -837 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { -110, 1721, -677 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 10, 1721, -677 }, { 0, -32767, 0 }, 1013 },
};

static const Oot3dRoomObjectEntry oot3d_hakadan_boss_info_hakadan_boss_0_info_objects[] = {
    { OBJECT_HAKA_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BDOOR, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SST, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_hakadan_boss_info_room_refs[] = {
    { "hakadan_boss_0_info.zsi", 0 },
    { "hakadan_boss_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_hakadan_boss_info_rooms[] = {
    { "hakadan_boss_0_info.zsi", 0, oot3d_hakadan_boss_info_hakadan_boss_0_info_objects, 6u, oot3d_hakadan_boss_info_hakadan_boss_0_info_actors, 6u },
    { "hakadan_boss_1_info.zsi", 1, NULL, 0u, NULL, 0u },
};

const Oot3dSceneIndex oot3d_scene_index_hakadan_boss_info = {
    "hakadan_boss_info.zsi",
    oot3d_hakadan_boss_info_room_refs, 2u,
    oot3d_hakadan_boss_info_rooms, 2u,
    NULL, 0u,
};
