#ifndef OOT3D_SCENE_LIGHT_SOURCE_TABLE_H
#define OOT3D_SCENE_LIGHT_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_LIGHT_SOURCE_ROW_COUNT = 1467,
    OOT3D_SCENE_LIGHT_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_LIGHT_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef struct {
    u16 lightSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u16 lightIndex;
    u32 offset;
    u8 raw[OOT3D_LIGHT_SETTINGS_RECORD_SIZE];
    u8 ambientR;
    u8 ambientG;
    u8 ambientB;
    s8 light0DirX;
    s8 light0DirY;
    s8 light0DirZ;
    u8 light0R;
    u8 light0G;
    u8 light0B;
    s8 light1DirX;
    s8 light1DirY;
    s8 light1DirZ;
    u8 light1R;
    u8 light1G;
    u8 light1B;
    u8 fogOrEnvironmentR;
    u8 fogOrEnvironmentG;
    u8 fogOrEnvironmentB;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* lightSettingsSymbol;
    const char* payloadSymbol;
    const char* layout;
    const char* validationStatus;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneLightSourceRow;

extern const Oot3dSceneLightSourceRow oot3d_scene_light_source_rows[];
extern const u32 oot3d_scene_light_source_row_count;

#endif
