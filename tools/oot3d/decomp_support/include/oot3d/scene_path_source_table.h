#ifndef OOT3D_SCENE_PATH_SOURCE_TABLE_H
#define OOT3D_SCENE_PATH_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_PATH_SOURCE_ROW_COUNT = 69,
    OOT3D_SCENE_PATH_POINT_SOURCE_ROW_COUNT = 975,
    OOT3D_SCENE_PATH_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_PATH_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef struct {
    u16 pointSourceIndex;
    u16 pathSourceIndex;
    u16 pointIndex;
    u32 offset;
    Oot3dVec3s pos;
} Oot3dScenePathPointSourceRow;

typedef struct {
    u16 pathSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u16 pathIndex;
    u32 offset;
    u8 raw[OOT3D_PATH_RECORD_SIZE];
    u8 pointCount;
    u8 unk01;
    u16 unk02;
    u32 rawPointsOffset;
    Oot3dPathPointsStatus pointsStatus;
    u16 pointRefStart;
    u16 pointRefCount;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* pathsSymbol;
    const char* payloadSymbol;
    const char* validationStatus;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dScenePathSourceRow;

extern const Oot3dScenePathPointSourceRow oot3d_scene_path_point_source_rows[];
extern const Oot3dScenePathSourceRow oot3d_scene_path_source_rows[];
extern const u32 oot3d_scene_path_point_source_row_count;
extern const u32 oot3d_scene_path_source_row_count;

#endif
