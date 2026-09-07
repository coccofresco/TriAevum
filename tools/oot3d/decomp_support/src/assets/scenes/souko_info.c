/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: souko_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_souko_info_souko_0_info_actors[] = {
    { ACTOR_EN_ITEM00, { -352, -2, -484 }, { 0, 0, 0 }, 262 },
    { ACTOR_BG_SPOT15_RRBOX, { -370, -10, -170 }, { 0, 0, 0 }, 255 },
    { ACTOR_BG_SPOT15_RRBOX, { -370, -10, -110 }, { 0, 0, 0 }, 255 },
    { ACTOR_BG_SPOT15_RRBOX, { -330, -10, -50 }, { 0, 0, 0 }, 255 },
    { ACTOR_BG_SPOT15_RRBOX, { -310, -10, -110 }, { 0, 0, 0 }, 255 },
    { ACTOR_BG_SPOT15_RRBOX, { -270, -10, -50 }, { 0, 0, 0 }, 255 },
    { ACTOR_BG_SPOT15_RRBOX, { -250, -10, -170 }, { 0, 0, 0 }, 255 },
    { ACTOR_EN_COW, { -142, 0, -140 }, { 0, 16384, 0 }, 0 },
    { ACTOR_EN_COW, { -108, 0, -65 }, { 0, 23848, 0 }, 0 },
};

static const Oot3dRoomObjectEntry oot3d_souko_info_souko_0_info_objects[] = {
    { OBJECT_SPOT15_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TSUBO, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_COW, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_souko_info_room_refs[] = {
    { "souko_0_info.zsi", 0 },
    { "souko_1_info.zsi", 1 },
    { "souko_2_info.zsi", 2 },
};

static const Oot3dRoomIndex oot3d_souko_info_rooms[] = {
    { "souko_0_info.zsi", 0, oot3d_souko_info_souko_0_info_objects, 3u, oot3d_souko_info_souko_0_info_actors, 9u },
    { "souko_1_info.zsi", 1, NULL, 0u, NULL, 0u },
    { "souko_2_info.zsi", 2, NULL, 0u, NULL, 0u },
};

const Oot3dSceneIndex oot3d_scene_index_souko_info = {
    "souko_info.zsi",
    oot3d_souko_info_room_refs, 3u,
    oot3d_souko_info_rooms, 3u,
    NULL, 0u,
};
