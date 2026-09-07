#ifndef OOT3D_SCENE_CUTSCENE_SET_TIME_TABLE_H
#define OOT3D_SCENE_CUTSCENE_SET_TIME_TABLE_H

#include "oot3d/scene.h"

enum {
    OOT3D_SCENE_CUTSCENE_SET_TIME_ROW_COUNT = 4,
};

typedef struct {
    u16 setTimeSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u32 commandOffset;
    u32 entryOffset;
    u16 unk00;
    u16 startFrame;
    u16 endFrame;
    u8 hour;
    u8 minute;
    u16 minutePlusOne;
    u32 unused;
    u16 dayTime;
    u16 skyboxTime;
    u32 rawWords[3];
    const char* scenePath;
    const char* runtimeSemantic;
    const char* rawWordsText;
} Oot3dSceneCutsceneSetTimeRow;

extern const Oot3dSceneCutsceneSetTimeRow oot3d_scene_cutscene_set_time_rows[];
extern const u32 oot3d_scene_cutscene_set_time_row_count;

u16 Oot3d_CutsceneSetTimeDecodeDayTime(u8 hour, u8 minute);

const Oot3dSceneCutsceneSetTimeRow* Oot3d_CutsceneSetTimeGetRow(
    u16 setTimeSourceIndex
);

u32 Oot3d_CutsceneSetTimeCollectStartTriggers(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutsceneSetTimeRow** outRows,
    u32 maxRows
);

#endif
