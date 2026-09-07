#ifndef OOT3D_SCENE_SETTING_SOURCE_TABLE_H
#define OOT3D_SCENE_SETTING_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_SETTING_SOURCE_ROW_COUNT = 782,
    OOT3D_SCENE_SETTING_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_SETTING_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef enum {
    OOT3D_SCENE_SETTING_UNKNOWN,
    OOT3D_SCENE_SETTING_SPECIAL_FILES,
    OOT3D_SCENE_SETTING_SKYBOX_SETTINGS,
    OOT3D_SCENE_SETTING_SOUND_SETTINGS,
    OOT3D_SCENE_SETTING_CUTSCENE_REFERENCE,
    OOT3D_SCENE_SETTING_MISC_SETTINGS,
} Oot3dSceneSettingKind;

typedef struct {
    u16 settingSourceIndex;
    Oot3dSceneSettingKind settingKind;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u8 commandId;
    u32 offset;
    u32 argument;
    u8 cUpElfMessageFile;
    s16 keepObjectId;
    const char* keepObjectName;
    u8 skyboxId;
    u8 weatherOrUnk05;
    u8 indoors;
    u8 soundSpecId;
    u16 soundData2Low16;
    u8 natureAmbienceId;
    u8 seqId;
    u32 cutsceneOffset;
    u8 cutsceneInFile;
    u8 cameraOrWorldMapArea;
    u32 miscRawArgument;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* payloadSymbol;
    const char* validationStatus;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
} Oot3dSceneSettingSourceRow;

extern const Oot3dSceneSettingSourceRow oot3d_scene_setting_source_rows[];
extern const u32 oot3d_scene_setting_source_row_count;

#endif
