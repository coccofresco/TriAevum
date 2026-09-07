#ifndef OOT3D_SCENE_ROOM_SOURCE_TABLE_H
#define OOT3D_SCENE_ROOM_SOURCE_TABLE_H

#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_ROOM_SOURCE_ROW_COUNT = 1050,
    OOT3D_SCENE_ROOM_SETUP_REF_COUNT = 334,
};

typedef struct {
    u16 roomSourceIndex;
    u16 setupSourceIndex;
    u16 setupIndex;
    u16 roomRefIndex;
    s16 roomIndex;
    const char* scenePath;
    const char* roomPath;
    const char* setupRole;
    const char* setupSymbol;
    const char* bindingStatus;
} Oot3dSceneRoomSetupRef;

typedef struct {
    u16 roomSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    s16 roomIndex;
    const char* scenePath;
    const char* sourceBasename;
    const char* sourceCFile;
    const char* sceneIndexSymbol;
    const char* roomPath;
    u32 roomSize;
    const char* roomActorSymbol;
    const char* roomObjectSymbol;
    const char* actorListStatus;
    const char* actorListConfidence;
    u16 actorCount;
    u16 objectCount;
    u16 unknownObjectIdCount;
    u16 setupRefStart;
    u16 setupRefCount;
    u16 uniqueSetupRefCount;
    u16 embeddedCmbCount;
    const char* objectNames;
    const char* actorNames;
} Oot3dSceneRoomSourceRow;

extern const Oot3dSceneRoomSetupRef oot3d_scene_room_setup_refs[];
extern const Oot3dSceneRoomSourceRow oot3d_scene_room_source_rows[];
extern const u32 oot3d_scene_room_setup_ref_count;
extern const u32 oot3d_scene_room_source_row_count;

#endif
