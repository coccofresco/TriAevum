#pragma once

#include "oot3d/title_intro_record_c_runtime.h"
#include "oot3d/title_intro_playback_runtime.h"
#include "oot3d/title_intro_source_selector_feed_runtime.h"
#include "oot3d/title_intro_source_selector_runtime.h"
#include "oot3d/title_intro_source_table.h"

#define OOT3D_TITLE_INTRO_STATE37_ACTIVE_ROW_MAX 8u
#define OOT3D_TITLE_INTRO_STATE37_QDB_ANY 0xFFFFu

typedef struct Oot3dTitleIntroCutsceneRuntimeState {
    Oot3dTitleIntroRecordByteTableState recordByteTable;
    Oot3dTitleIntroRecordTableState nativeRecordTable;
    Oot3dTitleIntroRecordCContext recordCContext;
    Oot3dTitleIntroSourceSelectorFeedState sourceSelectorFeed;
    Oot3dTitleIntroSourceSelectorState sourceSelector;
    Oot3dTitleIntroState37PlaybackState state37;
    u8 initialized;
} Oot3dTitleIntroCutsceneRuntimeState;

typedef struct Oot3dTitleIntroCutsceneRuntimeStep {
    s32 frame;
    u8 qdbIndexFiltered;
    u16 qdbIndex;
    u8 sourceSelectorFeedApplied;
    Oot3dTitleIntroSourceSelectorFeedStatus sourceSelectorFeedStatus;
    Oot3dTitleIntroSourceSelectorFeedEvent sourceSelectorFeedEvent;
    u8 sourceSelectorApplied;
    Oot3dTitleIntroSourceSelectorStatus sourceSelectorStatus;
    Oot3dTitleIntroSourceSelectorEvent sourceSelectorEvent;
    u8 recordCProduced;
    u8 producedRecordC[3];
    Oot3dTitleIntroRecordCStatus recordCStatus;
    Oot3dTitleIntroRecordCFrameEvent recordCEvent;
    u32 activeState37RowCount;
    const Oot3dTitleIntroLinkBoyPlayerActionRow* firstActiveState37Row;
    u8 state37PlaybackApplied;
    Oot3dTitleIntroPlaybackStatus state37Status;
    Oot3dTitleIntroState37Event state37Event;
} Oot3dTitleIntroCutsceneRuntimeStep;

void Oot3d_TitleIntroCutsceneRuntimeInit(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    u8* recordByteTableBytes,
    u32 recordByteTableByteCount,
    s16 state37Cursor
);

u32 Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37Rows(
    s32 frame,
    const Oot3dTitleIntroLinkBoyPlayerActionRow** outRows,
    u32 maxRows
);

u32 Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37RowsForQdb(
    s32 frame,
    u16 qdbIndex,
    const Oot3dTitleIntroLinkBoyPlayerActionRow** outRows,
    u32 maxRows
);

Oot3dTitleIntroRecordCContext* Oot3d_TitleIntroCutsceneRuntimeGetRecordCContext(
    Oot3dTitleIntroCutsceneRuntimeState* state
);

Oot3dTitleIntroSourceSelectorFeedState* Oot3d_TitleIntroCutsceneRuntimeGetSourceSelectorFeed(
    Oot3dTitleIntroCutsceneRuntimeState* state
);

Oot3dTitleIntroSourceSelectorState* Oot3d_TitleIntroCutsceneRuntimeGetSourceSelector(
    Oot3dTitleIntroCutsceneRuntimeState* state
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const u8 recordC[3],
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37ForQdb(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    u16 qdbIndex,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const u8 recordC[3],
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37FromContext(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37FromSelector(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37FromFeed(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroCutsceneRuntimeStepState37FromFeedForQdb(
    Oot3dTitleIntroCutsceneRuntimeState* state,
    s32 frame,
    u16 qdbIndex,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroCutsceneRuntimeStep* outStep
);
