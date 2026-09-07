/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: bowling_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_bowling_info_bowling_0_info_actors[] = {
    { ACTOR_BG_BOM_GUARD, { 10, 0, 160 }, { 0, 0, 0 }, -1 },
    { ACTOR_EN_BOM_BOWL_MAN, { 180, -12, 260 }, { 0, -16384, 0 }, -1 },
    { ACTOR_BG_BOWL_WALL, { 0, -100, -272 }, { 0, 0, 0 }, 0 },
    { ACTOR_EN_TRAP, { -135, -87, -176 }, { 0, 16384, 0 }, 1040 },
    { ACTOR_BG_BOWL_WALL, { 0, 97, -538 }, { 0, 0, 0 }, 1 },
};

static const Oot3dRoomObjectEntry oot3d_bowling_info_bowling_0_info_objects[] = {
    { OBJECT_DY_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_TRAP, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMB_1, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMB_2, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_BOMBPOUCH, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BG, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_HEARTS, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOWL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_GI_RUPY, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_NIW, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DOG, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_bowling_info_room_refs[] = {
    { "bowling_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_bowling_info_rooms[] = {
    { "bowling_0_info.zsi", 0, oot3d_bowling_info_bowling_0_info_objects, 11u, oot3d_bowling_info_bowling_0_info_actors, 5u },
};

const Oot3dSceneIndex oot3d_scene_index_bowling_info = {
    "bowling_info.zsi",
    oot3d_bowling_info_room_refs, 1u,
    oot3d_bowling_info_rooms, 1u,
    NULL, 0u,
};
