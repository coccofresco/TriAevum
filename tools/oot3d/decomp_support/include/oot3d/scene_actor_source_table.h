#ifndef OOT3D_SCENE_ACTOR_SOURCE_TABLE_H
#define OOT3D_SCENE_ACTOR_SOURCE_TABLE_H

#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_ACTOR_SOURCE_ROW_COUNT = 9087,
    OOT3D_SCENE_ACTOR_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_ACTOR_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef enum {
    OOT3D_SCENE_ACTOR_SOURCE_SETUP_SPAWN,
    OOT3D_SCENE_ACTOR_SOURCE_SETUP_STANDARD_ACTOR,
    OOT3D_SCENE_ACTOR_SOURCE_SETUP_TRANSITION_ACTOR,
    OOT3D_SCENE_ACTOR_SOURCE_ROOM_ACTOR,
    OOT3D_SCENE_ACTOR_SOURCE_UNKNOWN,
} Oot3dSceneActorSourceKind;

typedef struct {
    u16 actorSourceIndex;
    Oot3dSceneActorSourceKind sourceKind;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 roomSourceIndex;
    s16 setupIndex;
    s16 roomIndex;
    u16 sourceIndex;
    u32 sourceOffset;
    s16 actorId;
    const char* actorName;
    s16 posX;
    s16 posY;
    s16 posZ;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    s16 params;
    s8 frontRoom;
    s8 frontEffect;
    s8 backRoom;
    s8 backEffect;
    const char* scenePath;
    const char* roomPath;
    const char* setupRole;
    const char* sourceStatus;
    const char* selectedStatus;
    const char* confidence;
    const char* validationStatus;
} Oot3dSceneActorSourceRow;

extern const Oot3dSceneActorSourceRow oot3d_scene_actor_source_rows[];
extern const u32 oot3d_scene_actor_source_row_count;

#endif
