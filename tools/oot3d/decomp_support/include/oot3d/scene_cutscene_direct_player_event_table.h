#ifndef OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_TABLE_H
#define OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_TABLE_H

#include "oot3d/scene.h"

enum {
    OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_ROW_COUNT = 73,
    OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_DYNAMIC_VARIANT_ROW_COUNT = 6,
};

typedef enum {
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0005 = 0x0005,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001C = 0x001C,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001D = 0x001D,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001E = 0x001E,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001F = 0x001F,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0023 = 0x0023,
    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0068 = 0x0068,
} Oot3dCutsceneDirectPlayerEventId;

typedef struct {
    u16 directPlayerEventSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u32 commandOffset;
    u32 payloadOffset;
    u16 actionId;
    u16 startFrame;
    u16 unk0C;
    u16 unk0E;
    u8 normalStartFrameGateConfirmed;
    u8 dispatchResolved;
    u16 transitionRequestIndex;
    u8 transitionRequestTrigger;
    u8 transitionRequestEffect;
    u32 playerRuntimeField8Value;
    u8 playerRuntimeField8ValueResolved;
    u16 playerByteWriteOffset;
    u8 playerByteWriteValue;
    u8 playerByteWriteResolved;
    u16 itemGiveId;
    u8 itemGiveResolved;
    u32 rawWords[4];
    const char* scenePath;
    const char* actionName;
    const char* runtimeSemantic;
    const char* transitionRequestRole;
    const char* playerRuntimeField8Expression;
    const char* dispatchEvidence;
    const char* rawWordsText;
    u8 dynamicDispatchResolved;
    u16 dynamicDispatchVariantRefStart;
    u16 dynamicDispatchVariantRefCount;
    u32 dynamicDispatchStateAddress;
    u8 dynamicDispatchInitialState;
    const char* dynamicDispatchStateExpression;
} Oot3dSceneCutsceneDirectPlayerEventRow;

typedef struct {
    u16 dynamicDispatchVariantRefIndex;
    u16 directPlayerEventSourceIndex;
    u8 selectorValue;
    u8 selectorIncremented;
    u8 selectorResetToZero;
    u16 transitionRequestIndex;
    u8 transitionRequestTrigger;
    u8 transitionRequestEffect;
    u32 playerRuntimeField8Value;
    u8 playerRuntimeField8ValueResolved;
    const char* playerRuntimeField8Expression;
    const char* dispatchEvidence;
} Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow;

extern const Oot3dSceneCutsceneDirectPlayerEventRow oot3d_scene_cutscene_direct_player_event_rows[];
extern const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow
    oot3d_scene_cutscene_direct_player_event_dynamic_variant_rows[];
extern const u32 oot3d_scene_cutscene_direct_player_event_row_count;
extern const u32 oot3d_scene_cutscene_direct_player_event_dynamic_variant_row_count;

const Oot3dSceneCutsceneDirectPlayerEventRow* Oot3d_CutsceneDirectPlayerEventGetRow(
    u16 directPlayerEventSourceIndex
);

u32 Oot3d_CutsceneDirectPlayerEventCollectStartTriggers(
    u16 cutsceneSourceIndex,
    s32 frame,
    const Oot3dSceneCutsceneDirectPlayerEventRow** outRows,
    u32 maxRows
);

const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow*
Oot3d_CutsceneDirectPlayerEventFindDynamicVariant(
    const Oot3dSceneCutsceneDirectPlayerEventRow* row,
    u8 selectorValue
);

#endif
