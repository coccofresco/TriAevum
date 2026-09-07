#ifndef OOT3D_SCENE_TRANSITION_H
#define OOT3D_SCENE_TRANSITION_H

#include "oot3d/types.h"

enum {
    OOT3D_SCENE_EXIT_DIRECT_LIMIT = 0x7FF9,
    OOT3D_SCENE_EXIT_HIGH_REMAP_BASE = 0x7FF9,
    OOT3D_SCENE_EXIT_HIGH_REMAP_COUNT = 6,
    OOT3D_SCENE_EXIT_SPECIAL_7FFF = 0x7FFF,
    OOT3D_SCENE_EXIT_HIGH_REMAP_VERIFIED_ENTRANCE_COUNT = 14,
    OOT3D_SCENE_TRANSITION_REQUEST_STATE = 0x14,
    OOT3D_PLAY_ENTRANCE_INDEX_OFFSET = 0x5C02,
    OOT3D_PLAY_PENDING_TRANSITION_VALUE_OFFSET = 0x5C32,
    OOT3D_PLAY_PENDING_TRANSITION_STATE_OFFSET = 0x5C2D,
    OOT3D_PLAY_PENDING_TRANSITION_EFFECT_OFFSET = 0x5C76,
};

typedef struct {
    u16 exitValue;
    u8 entranceIndexDelta;
} Oot3dSceneExitHighRemapDelta;

typedef struct {
    u8 tableIndex;
    s16 entrance;
} Oot3dSceneExitHighRemapEntrance;

typedef struct {
    const Oot3dSceneExitHighRemapDelta* deltas;
    u32 deltaCount;
    const Oot3dSceneExitHighRemapEntrance* verifiedEntrances;
    u32 verifiedEntranceCount;
} Oot3dSceneExitHighRemapTables;

extern const Oot3dSceneExitHighRemapDelta oot3d_scene_exit_high_remap_deltas[];
extern const Oot3dSceneExitHighRemapEntrance oot3d_scene_exit_high_remap_verified_entrances[];
extern const Oot3dSceneExitHighRemapTables oot3d_scene_exit_high_remap_tables;

#endif
