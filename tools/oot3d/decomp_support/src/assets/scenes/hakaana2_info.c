/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hakaana2_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_hakaana2_info_hakaana2_0_info_actors[] = {
    { ACTOR_EN_DOOR, { -3, 0, 0 }, { 0, 19, 89 }, -28 },
    { ACTOR_EN_HORSE, { 386, 0, 0 }, { 0, -24575, 94 }, 37 },
    { ACTOR_EN_HORSE, { 614, 0, 0 }, { 0, 9216, 94 }, -84 },
    { ACTOR_EN_HORSE, { 614, 0, 0 }, { 0, 9216, 10 }, -22 },
};

static const Oot3dRoomObjectEntry oot3d_hakaana2_info_hakaana2_0_info_objects[] = {
    { OBJECT_BWALL, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_DY_OBJ, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OA3, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { 987, OOT3D_ROOM_OBJECT_UNKNOWN_OOT3D_ID },
};

static const Oot3dRoomReference oot3d_hakaana2_info_room_refs[] = {
    { "hakaana2_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_hakaana2_info_rooms[] = {
    { "hakaana2_0_info.zsi", 0, oot3d_hakaana2_info_hakaana2_0_info_objects, 6u, oot3d_hakaana2_info_hakaana2_0_info_actors, 4u },
};

const Oot3dSceneIndex oot3d_scene_index_hakaana2_info = {
    "hakaana2_info.zsi",
    oot3d_hakaana2_info_room_refs, 1u,
    oot3d_hakaana2_info_rooms, 1u,
    NULL, 0u,
};
