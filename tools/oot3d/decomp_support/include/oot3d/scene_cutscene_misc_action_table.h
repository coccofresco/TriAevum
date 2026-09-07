#ifndef OOT3D_SCENE_CUTSCENE_MISC_ACTION_TABLE_H
#define OOT3D_SCENE_CUTSCENE_MISC_ACTION_TABLE_H

#include "oot3d/scene.h"

enum {
    OOT3D_SCENE_CUTSCENE_MISC_ACTION_ROW_COUNT = 125,
};

typedef enum {
    OOT3D_CUTSCENE_MISC_ACTION_START_LIGHT_MODE_BLEND_0_TO_1 = 0x0007,
    OOT3D_CUTSCENE_MISC_ACTION_RAMP_53F0_SMALL = 0x0008,
    OOT3D_CUTSCENE_MISC_ACTION_SET_3271_TO_10 = 0x0009,
    OOT3D_CUTSCENE_MISC_ACTION_RAMP_53F0_TIMED_SFX = 0x000B,
    OOT3D_CUTSCENE_MISC_ACTION_REQUEST_CUTSCENE_END = 0x000C,
    OOT3D_CUTSCENE_MISC_ACTION_SET_CAMERA_DATA_INDEX_0 = 0x000E,
    OOT3D_CUTSCENE_MISC_ACTION_START_CAMERA_QUAKE = 0x0010,
    OOT3D_CUTSCENE_MISC_ACTION_STOP_CAMERA_QUAKE = 0x0011,
    OOT3D_CUTSCENE_MISC_ACTION_LIGHT_COLOR_ADDEND_RAMP = 0x001B,
    OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_3 = 0x001E,
    OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_4 = 0x001F,
} Oot3dCutsceneMiscActionId;

typedef struct {
    u16 miscActionSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u32 commandOffset;
    u32 entryOffset;
    u16 actionId;
    u16 startFrame;
    u16 endFrame;
    u16 durationFrames;
    u8 startFrameTriggerConfirmed;
    u8 activeWindowInclusiveStartExclusiveEnd;
    const char* scenePath;
    const char* actionName;
    const char* runtimeSemantic;
    const char* rawWordsText;
} Oot3dSceneCutsceneMiscActionRow;

extern const Oot3dSceneCutsceneMiscActionRow oot3d_scene_cutscene_misc_action_rows[];
extern const u32 oot3d_scene_cutscene_misc_action_row_count;

const Oot3dSceneCutsceneMiscActionRow* Oot3d_CutsceneMiscActionGetRow(u16 miscActionSourceIndex);

u32 Oot3d_CutsceneMiscActionCollectActiveWindows(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutsceneMiscActionRow** outRows,
    u32 maxRows
);

u32 Oot3d_CutsceneMiscActionCollectStartTriggers(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutsceneMiscActionRow** outRows,
    u32 maxRows
);

#endif
