#ifndef OOT3D_SCENE_COMMAND_SOURCE_TABLE_H
#define OOT3D_SCENE_COMMAND_SOURCE_TABLE_H

#include "oot3d/types.h"

enum {
    OOT3D_SCENE_COMMAND_SOURCE_ROW_COUNT = 2132,
    OOT3D_SCENE_COMMAND_HANDLER_ROW_COUNT = 14,
    OOT3D_SCENE_COMMAND_SOURCE_UNMAPPED_INDEX = 0xFFFF,
};

typedef enum {
    OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW,
    OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW,
    OOT3D_SCENE_COMMAND_BINDING_UNMAPPED,
} Oot3dSceneCommandBindingKind;

typedef struct {
    u8 commandId;
    const char* commandName;
    u32 handlerEntry;
    const char* handlerName;
    const char* supportLevel;
    u16 runtimeStoreCount;
} Oot3dSceneCommandHandlerRow;

typedef struct {
    u16 commandSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupIndex;
    u16 commandIndex;
    u32 offset;
    u8 commandId;
    u8 parameter;
    u32 argument;
    const char* scenePath;
    const char* sourceBasename;
    const char* sceneIndexSymbol;
    const char* commandName;
    const char* decodedStatus;
    const char* supportLevel;
    const char* payloadSymbol;
    const char* exportStructs;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneCommandSourceRow;

extern const Oot3dSceneCommandHandlerRow oot3d_scene_command_handler_rows[];
extern const Oot3dSceneCommandSourceRow oot3d_scene_command_source_rows[];
extern const u32 oot3d_scene_command_handler_row_count;
extern const u32 oot3d_scene_command_source_row_count;

#endif
