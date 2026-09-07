#ifndef OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_TABLE_H
#define OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_TABLE_H

#include "oot3d/scene.h"

enum {
    OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_ROW_COUNT = 476,
};

typedef enum {
    OOT3D_CUTSCENE_PLAYER_ACTION_ID_0005 = 0x0005,
    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001C = 0x001C,
    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001D = 0x001D,
    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001E = 0x001E,
    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001F = 0x001F,
} Oot3dCutscenePlayerActionId;

typedef struct {
    u16 playerActionSourceIndex;
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
    u16 unk06;
    u8 activeWindowStartExclusiveEndInclusive;
    u32 rawWords[12];
    const char* scenePath;
    const char* actionName;
    const char* runtimeSemantic;
    const char* rawWordsText;
} Oot3dSceneCutscenePlayerActionRow;

extern const Oot3dSceneCutscenePlayerActionRow oot3d_scene_cutscene_player_action_rows[];
extern const u32 oot3d_scene_cutscene_player_action_row_count;

const Oot3dSceneCutscenePlayerActionRow* Oot3d_CutscenePlayerActionGetRow(
    u16 playerActionSourceIndex
);

u32 Oot3d_CutscenePlayerActionCollectActiveWindows(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutscenePlayerActionRow** outRows,
    u32 maxRows
);

u32 Oot3d_CutscenePlayerActionCollectStartTriggers(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutscenePlayerActionRow** outRows,
    u32 maxRows
);

#endif
