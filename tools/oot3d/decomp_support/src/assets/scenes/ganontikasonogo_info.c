/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: ganontikasonogo_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_ganontikasonogo_info_ganontikasonogo_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 867, 226, -824 }, { 0, 0, 0 }, 18 },
    { ACTOR_EN_ZL3, { 746, 226, -837 }, { 0, -16384, 0 }, 10499 },
    { ACTOR_EN_EG, { 903, 226, -788 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ZG, { 1292, 196, -840 }, { 0, -16384, 0 }, 10504 },
    { ACTOR_EN_RD, { 942, 220, -912 }, { 0, 0, 0 }, 32513 },
    { ACTOR_EN_FIRE_ROCK, { 783, 226, -908 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_FIRE_ROCK, { 1070, 202, -746 }, { 0, 0, 0 }, 6 },
};

static const Oot3dRoomObjectEntry oot3d_ganontikasonogo_info_ganontikasonogo_0_info_objects[] = {
    { OBJECT_GANON_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZL2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZL2_ANIME2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GANON_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_STAR_FIELD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dActorEntry oot3d_ganontikasonogo_info_ganontikasonogo_1_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { 2709, 506, -820 }, { 0, 0, 0 }, 18 },
    { ACTOR_EN_ZL3, { 1665, 206, -837 }, { 0, -16384, 0 }, 10771 },
    { ACTOR_EN_EG, { 1578, 196, -837 }, { 0, 0, 0 }, 0 },
    { ACTOR_BG_ZG, { 2832, 506, -840 }, { 0, -16384, 0 }, 10761 },
    { ACTOR_EN_FIRE_ROCK, { 1986, 446, -860 }, { 0, 0, 0 }, 5 },
    { ACTOR_EN_FIRE_ROCK, { 2429, 576, -866 }, { 0, 0, 0 }, 5 },
    { ACTOR_EN_FIRE_ROCK, { 1777, 256, -799 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_FIRE_ROCK, { 2062, 296, -884 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_FIRE_ROCK, { 2198, 386, -784 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_FIRE_ROCK, { 2495, 446, -882 }, { 0, 0, 0 }, 6 },
    { ACTOR_EN_LIGHT, { 1931, 338, -770 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 1933, 338, -910 }, { 0, 0, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 2370, 468, -770 }, { 0, -32767, 0 }, 1013 },
    { ACTOR_EN_LIGHT, { 2374, 468, -910 }, { 0, 0, 0 }, 1013 },
};

static const Oot3dRoomObjectEntry oot3d_ganontikasonogo_info_ganontikasonogo_1_info_objects[] = {
    { OBJECT_GANON_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZL2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZL2_ANIME2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GANON_OBJECTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_ZG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_EFC_STAR_FIELD, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_ganontikasonogo_info_room_refs[] = {
    { "ganontikasonogo_0_info.zsi", 0 },
    { "ganontikasonogo_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_ganontikasonogo_info_rooms[] = {
    { "ganontikasonogo_0_info.zsi", 0, oot3d_ganontikasonogo_info_ganontikasonogo_0_info_objects, 7u, oot3d_ganontikasonogo_info_ganontikasonogo_0_info_actors, 7u },
    { "ganontikasonogo_1_info.zsi", 1, oot3d_ganontikasonogo_info_ganontikasonogo_1_info_objects, 6u, oot3d_ganontikasonogo_info_ganontikasonogo_1_info_actors, 14u },
};

const Oot3dSceneIndex oot3d_scene_index_ganontikasonogo_info = {
    "ganontikasonogo_info.zsi",
    oot3d_ganontikasonogo_info_room_refs, 2u,
    oot3d_ganontikasonogo_info_rooms, 2u,
    NULL, 0u,
};
