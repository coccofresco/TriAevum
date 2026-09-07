#ifndef OOT3D_SCENE_OBJECT_SOURCE_TABLE_H
#define OOT3D_SCENE_OBJECT_SOURCE_TABLE_H

#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_OBJECT_SOURCE_ROW_COUNT = 6573,
    OOT3D_SCENE_OBJECT_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_OBJECT_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef struct {
    u16 objectSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 roomSourceIndex;
    s16 roomIndex;
    u16 objectIndex;
    u16 objectId;
    u32 offset;
    u32 prefixStartOffset;
    u32 prefixEndOffset;
    u16 prefixByteCount;
    const char* objectName;
    const char* semanticStatus;
    const char* validationStatus;
    const char* scenePath;
    const char* roomPath;
    const char* roomObjectSymbol;
    const char* actorListStatus;
    const char* actorListConfidence;
} Oot3dSceneObjectSourceRow;

extern const Oot3dSceneObjectSourceRow oot3d_scene_object_source_rows[];
extern const u32 oot3d_scene_object_source_row_count;

#endif
