/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: ydan_boss_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_ydan_boss_info_ydan_boss_1_info_actors[] = {
    { ACTOR_BOSS_GOMA, { -160, -280, -371 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_KUSA, { 194, -640, -974 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 480, -640, -681 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -855, -640, -596 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -848, -640, -166 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -444, -640, 323 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { -646, -640, 108 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 525, -640, -375 }, { 0, 0, 0 }, -255 },
    { ACTOR_EN_KUSA, { 545, -640, -75 }, { 0, 0, 0 }, -255 },
};

static const Oot3dRoomObjectEntry oot3d_ydan_boss_info_ydan_boss_1_info_objects[] = {
    { OBJECT_YDAN_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOMA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GOL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_WARP1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KUSA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_ydan_boss_info_room_refs[] = {
    { "ydan_boss_0_info.zsi", 0 },
    { "ydan_boss_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_ydan_boss_info_rooms[] = {
    { "ydan_boss_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "ydan_boss_1_info.zsi", 1, oot3d_ydan_boss_info_ydan_boss_1_info_objects, 7u, oot3d_ydan_boss_info_ydan_boss_1_info_actors, 9u },
};

const Oot3dSceneIndex oot3d_scene_index_ydan_boss_info = {
    "ydan_boss_info.zsi",
    oot3d_ydan_boss_info_room_refs, 2u,
    oot3d_ydan_boss_info_rooms, 2u,
    NULL, 0u,
};
