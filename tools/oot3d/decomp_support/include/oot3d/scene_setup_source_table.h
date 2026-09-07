#ifndef OOT3D_SCENE_SETUP_SOURCE_TABLE_H
#define OOT3D_SCENE_SETUP_SOURCE_TABLE_H

#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_SETUP_SOURCE_ROW_COUNT = 175,
    OOT3D_SCENE_SETUP_COMMAND_REF_COUNT = 2132,
};

typedef struct {
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 commandIndex;
    u8 commandId;
} Oot3dSceneSetupCommandRef;

typedef struct {
    u16 setupSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupIndex;
    const char* scenePath;
    const char* sourceBasename;
    const char* sceneIndexSymbol;
    const char* setupRole;
    const char* setupSymbol;
    u16 commandRefStart;
    u16 commandRefCount;
    u16 handlerResolvedCount;
    u16 handlerMissingCount;
    u16 nativeControlMarkerCount;
    u16 decodedCountMismatchCount;
    const char* spawnsSymbol;
    const char* entrancesSymbol;
    const char* exitsSymbol;
    const char* transitionActorsSymbol;
    const char* lightSettingsSymbol;
    const char* pathsSymbol;
    const char* cutscenesSymbol;
} Oot3dSceneSetupSourceRow;

extern const Oot3dSceneSetupCommandRef oot3d_scene_setup_command_refs[];
extern const Oot3dSceneSetupSourceRow oot3d_scene_setup_source_rows[];
extern const u32 oot3d_scene_setup_command_ref_count;
extern const u32 oot3d_scene_setup_source_row_count;

#endif
