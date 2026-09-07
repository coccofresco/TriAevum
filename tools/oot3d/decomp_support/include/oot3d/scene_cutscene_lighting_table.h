#ifndef OOT3D_SCENE_CUTSCENE_LIGHTING_TABLE_H
#define OOT3D_SCENE_CUTSCENE_LIGHTING_TABLE_H

#include "oot3d/scene.h"

enum {
    OOT3D_SCENE_CUTSCENE_LIGHTING_ROW_COUNT = 133,
    OOT3D_CUTSCENE_LIGHTING_PLAY_TARGET_LIGHT_SETTING_OFFSET = 0x3237,
    OOT3D_CUTSCENE_LIGHTING_PLAY_BLEND_WEIGHT_OFFSET = 0x3258,
    OOT3D_CUTSCENE_LIGHTING_TARGET_INVALID_VALUE = 0xFF,
};

typedef struct {
    u16 lightingSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u32 commandOffset;
    u32 entryOffset;
    u16 rawLightSettingIndex;
    u8 targetLightSetting;
    u8 targetLightSettingResolved;
    u8 targetLightSettingIsSentinel;
    u16 playTargetLightSettingOffset;
    u16 playBlendWeightOffset;
    u16 startFrame;
    u16 endFrame;
    u8 startFrameTriggerConfirmed;
    const char* scenePath;
    const char* runtimeSemantic;
    const char* rawWordsText;
} Oot3dSceneCutsceneLightingRow;

extern const Oot3dSceneCutsceneLightingRow oot3d_scene_cutscene_lighting_rows[];
extern const u32 oot3d_scene_cutscene_lighting_row_count;

const Oot3dSceneCutsceneLightingRow* Oot3d_CutsceneLightingGetRow(u16 lightingSourceIndex);

u32 Oot3d_CutsceneLightingCollectStartTriggers(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutsceneLightingRow** outRows,
    u32 maxRows
);

#endif
