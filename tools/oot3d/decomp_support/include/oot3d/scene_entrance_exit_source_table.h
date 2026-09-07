#ifndef OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_TABLE_H
#define OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_ENTRANCE_SOURCE_ROW_COUNT = 644,
    OOT3D_SCENE_EXIT_SOURCE_ROW_COUNT = 666,
    OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef struct {
    u16 entranceSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 roomSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u16 entranceIndex;
    u32 offset;
    u8 spawn;
    s8 room;
    u8 roomU8;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* payloadSymbol;
    const char* roomPath;
    const char* roomResolutionStatus;
    const char* validationStatus;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneEntranceSourceRow;

typedef struct {
    u16 exitSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u16 exitIndex;
    u32 offset;
    s16 value;
    u16 rawU16;
    Oot3dExitCategory category;
    u16 targetGlobalEntranceIndex;
    u8 targetSceneId;
    u16 targetLocalEntranceIndex;
    s16 highRemapDelta;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* payloadSymbol;
    const char* targetScenePath;
    const char* targetSceneIndexSymbol;
    const char* targetResolutionStatus;
    const char* validationStatus;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneExitSourceRow;

extern const Oot3dSceneEntranceSourceRow oot3d_scene_entrance_source_rows[];
extern const Oot3dSceneExitSourceRow oot3d_scene_exit_source_rows[];
extern const u32 oot3d_scene_entrance_source_row_count;
extern const u32 oot3d_scene_exit_source_row_count;

#endif
