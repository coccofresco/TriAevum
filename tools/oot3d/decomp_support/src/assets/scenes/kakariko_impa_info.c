/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: kakariko_impa_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_kakariko_impa_info_kakariko_impa_0_info_actors[] = {
    { ACTOR_EN_RIVER_SOUND, { -178, 0, 119 }, { 0, 0, 0 }, 20 },
    { ACTOR_EN_HY, { 74, 0, -32 }, { 0, -5643, 0 }, 1920 },
    { ACTOR_OBJ_KIBAKO2, { 176, 0, 170 }, { 0, 16384, 32 }, -1 },
};

static const Oot3dRoomObjectEntry oot3d_kakariko_impa_info_kakariko_impa_0_info_objects[] = {
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OS_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_AHG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_CNE, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BBA, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BJI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOB, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_KIBAKO2, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_kakariko_impa_info_room_refs[] = {
    { "kakariko_impa_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_kakariko_impa_info_rooms[] = {
    { "kakariko_impa_0_info.zsi", 0, oot3d_kakariko_impa_info_kakariko_impa_0_info_objects, 11u, oot3d_kakariko_impa_info_kakariko_impa_0_info_actors, 3u },
};

const Oot3dSceneIndex oot3d_scene_index_kakariko_impa_info = {
    "kakariko_impa_info.zsi",
    oot3d_kakariko_impa_info_room_refs, 1u,
    oot3d_kakariko_impa_info_rooms, 1u,
    NULL, 0u,
};
