/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: hairal_niwa_n_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dActorEntry oot3d_hairal_niwa_n_info_hairal_niwa_n_0_info_actors[] = {
    { ACTOR_EN_HEISHI1, { 1924, 0, 122 }, { 0, 17476, 0 }, 1535 },
    { ACTOR_EN_HEISHI1, { 1989, 0, 218 }, { 0, 20388, 0 }, 1535 },
};

static const Oot3dRoomObjectEntry oot3d_hairal_niwa_n_info_hairal_niwa_n_0_info_objects[] = {
    { OBJECT_O_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OE_ANIME, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_OA10, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_SD, OOT3D_ROOM_OBJECT_KNOWN_ID },
    { OBJECT_NIW, OOT3D_ROOM_OBJECT_KNOWN_ID },
};

static const Oot3dRoomReference oot3d_hairal_niwa_n_info_room_refs[] = {
    { "hairal_niwa_n_0_info.zsi", 0 },
};

static const Oot3dRoomIndex oot3d_hairal_niwa_n_info_rooms[] = {
    { "hairal_niwa_n_0_info.zsi", 0, oot3d_hairal_niwa_n_info_hairal_niwa_n_0_info_objects, 5u, oot3d_hairal_niwa_n_info_hairal_niwa_n_0_info_actors, 2u },
};

const Oot3dSceneIndex oot3d_scene_index_hairal_niwa_n_info = {
    "hairal_niwa_n_info.zsi",
    oot3d_hairal_niwa_n_info_room_refs, 1u,
    oot3d_hairal_niwa_n_info_rooms, 1u,
    NULL, 0u,
};
