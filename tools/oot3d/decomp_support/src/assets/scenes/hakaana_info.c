/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hakaana_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_hakaana_info_hakaana_0_info_actors[] = {
    { ACTOR_EN_BOX, { 2, -20, -315 }, { 0, -32767, 0 }, -22592 },
    { ACTOR_EN_RD, { 2, -20, -250 }, { 0, 0, 0 }, 32514 },
    { ACTOR_OBJ_SYOKUDAI, { 60, -20, -186 }, { 0, 0, 0 }, 9216 },
    { ACTOR_OBJ_SYOKUDAI, { -61, -20, -186 }, { 0, 0, 0 }, 9216 },
};

static const Oot3dRoomObjectEntry oot3d_hakaana_info_hakaana_0_info_objects[] = {
    { OBJECT_TK, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_BOX, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_RD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SYOKUDAI, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_hakaana_info_room_refs[] = {
    { "hakaana_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_hakaana_info_rooms[] = {
    { "hakaana_0_info.zsi", 0, oot3d_hakaana_info_hakaana_0_info_objects, 4u, oot3d_hakaana_info_hakaana_0_info_actors, 4u },
};

const Oot3dSceneIndex oot3d_scene_index_hakaana_info = {
    "hakaana_info.zsi",
    oot3d_hakaana_info_room_refs, 1u,
    oot3d_hakaana_info_rooms, 1u,
    NULL, 0u,
};
