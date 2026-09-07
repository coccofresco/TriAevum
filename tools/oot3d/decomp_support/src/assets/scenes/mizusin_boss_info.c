/*
 * Generated from native OOT3D ZSI scene data.
 * Source scene: mizusin_boss_info.zsi
 * These tables are decompilation support data, not N64 asset substitutions.
 */
#include "oot3d/scene.h"
#include "oot3d/actor_object_semantics.h"

static const Oot3dRoomReference oot3d_mizusin_boss_info_room_refs[] = {
    { "mizusin_boss_0_info.zsi", 0 },
    { "mizusin_boss_1_info.zsi", 1 },
};

static const Oot3dRoomIndex oot3d_mizusin_boss_info_rooms[] = {
    { "mizusin_boss_0_info.zsi", 0, NULL, 0u, NULL, 0u },
    { "mizusin_boss_1_info.zsi", 1, NULL, 0u, NULL, 0u },
};

const Oot3dSceneIndex oot3d_scene_index_mizusin_boss_info = {
    "mizusin_boss_info.zsi",
    oot3d_mizusin_boss_info_room_refs, 2u,
    oot3d_mizusin_boss_info_rooms, 2u,
    NULL, 0u,
};
