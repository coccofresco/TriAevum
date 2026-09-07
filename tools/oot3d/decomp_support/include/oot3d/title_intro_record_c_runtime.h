#pragma once

#include "oot3d/title_intro_runtime_tables.h"
#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_RECORD_CONTEXT_ADDR 0x0054AC48u
#define OOT3D_TITLE_INTRO_RECORD_TABLE_ADDR 0x0054AC9Du
#define OOT3D_TITLE_INTRO_RECORD_A_ADDR 0x0054AC9Du
#define OOT3D_TITLE_INTRO_RECORD_B_ADDR 0x0054ACA0u
#define OOT3D_TITLE_INTRO_RECORD_C_ADDR 0x0054ACA3u

#define OOT3D_TITLE_INTRO_CONTEXT_CURRENT_SLOT_OFFSET 0x0016u
#define OOT3D_TITLE_INTRO_CONTEXT_RECORD_SOURCE_OFFSET 0x0018u
#define OOT3D_TITLE_INTRO_CONTEXT_CURRENT_Z_OFFSET 0x001Au
#define OOT3D_TITLE_INTRO_CONTEXT_CURRENT_X_OFFSET 0x001Bu
#define OOT3D_TITLE_INTRO_CONTEXT_CURRENT_Y_OFFSET 0x001Cu
#define OOT3D_TITLE_INTRO_CONTEXT_RECORD_B_BYTE1_SOURCE_OFFSET 0x001Du
#define OOT3D_TITLE_INTRO_CONTEXT_MODE_OFFSET 0x0024u
#define OOT3D_TITLE_INTRO_CONTEXT_PENDING_HISTORY_INDEX_OFFSET 0x0025u
#define OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_SLOT_OFFSET 0x0026u
#define OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_X_OFFSET 0x0027u
#define OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_Y_OFFSET 0x0028u
#define OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_Z_OFFSET 0x0029u
#define OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_SOURCE_FLAGS_OFFSET 0x002Au
#define OOT3D_TITLE_INTRO_CONTEXT_DIRTY_FLAG_OFFSET 0x002Du
#define OOT3D_TITLE_INTRO_CONTEXT_RECORD_CURSOR_OFFSET 0x003Eu
#define OOT3D_TITLE_INTRO_CONTEXT_LAST_PROGRESS_TIME_OFFSET 0x00ACu
#define OOT3D_TITLE_INTRO_CONTEXT_CURRENT_PROGRESS_TIME_OFFSET 0x00D8u

#define OOT3D_TITLE_INTRO_RECORD_C_PROGRESS_DELTA_MIN 2u
#define OOT3D_TITLE_INTRO_RECORD_C_CURSOR_LIMIT 8u
#define OOT3D_TITLE_INTRO_RECORD_C_MODE_2 2u
#define OOT3D_TITLE_INTRO_RECORD_C_MODE_MATCH 0xFFu
#define OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT 0xFFu
#define OOT3D_TITLE_INTRO_RECORD_C_BYTE0_MASK 0x3Fu
#define OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY 0x6Bu
#define OOT3D_TITLE_INTRO_RECORD_C_HISTORY_SCAN_LIMIT 0x10u
#define OOT3D_TITLE_INTRO_RECORD_C_PATTERN_WINDOW_COUNT 8u
#define OOT3D_TITLE_INTRO_RECORD_C_PATTERN_NONE 0xFFu
#define OOT3D_TITLE_INTRO_RECORD_C_SOURCE_FLAGS_HI2_MASK 0xC0u
#define OOT3D_TITLE_INTRO_RECORD_C_GLOBAL_PATTERN_MATCH_VALUE 0xFFu

typedef enum {
    OOT3D_TITLE_INTRO_RECORD_C_OK = 0,
    OOT3D_TITLE_INTRO_RECORD_C_NULL_CONTEXT,
    OOT3D_TITLE_INTRO_RECORD_C_NULL_TABLE,
    OOT3D_TITLE_INTRO_RECORD_C_NULL_OUTPUT,
} Oot3dTitleIntroRecordCStatus;

typedef struct Oot3dTitleIntroRecordTriplet {
    u8 byte0;
    u8 byte1;
    u8 byte2;
} Oot3dTitleIntroRecordTriplet;

typedef struct Oot3dTitleIntroRecordTableState {
    Oot3dTitleIntroRecordTriplet recordA;
    Oot3dTitleIntroRecordTriplet recordB;
    Oot3dTitleIntroRecordTriplet recordC;
} Oot3dTitleIntroRecordTableState;

typedef struct Oot3dTitleIntroRecordCContext {
    u8 currentSlotOrId;
    u8 recordSourceByte;
    u8 currentX;
    u8 currentY;
    u8 currentZ;
    u8 recordBByte1Source;
    u8 modeByte;
    u8 pendingHistoryIndex;
    u8 previousSlot;
    u8 previousX;
    u8 previousY;
    u8 previousZ;
    u8 previousSourceFlags;
    u8 dirtyFlag;
    u8 cursorCount;
    u8 globalPatternFlag;
    u8 historyCompactionActiveClearRequested;
    Oot3dTitleIntroHistoryRecord historyB0Rows[OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY];
    Oot3dTitleIntroHistoryRecord historyB4Rows[OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY];
    u8 historyPatternWindow[OOT3D_TITLE_INTRO_RECORD_C_PATTERN_WINDOW_COUNT];
    u32 lastProgressTime;
    u32 currentProgressTime;
} Oot3dTitleIntroRecordCContext;

typedef struct Oot3dTitleIntroRecordCHistoryCommitEvent {
    u8 requested;
    u8 forced;
    u8 mode1BufferSelected;
    u8 previousSlot;
    s16 durationDelta;
    u8 previousX;
    u8 previousY;
    u8 previousZ;
    u8 previousSourceFlagsHi2;
    u8 pendingIndexBefore;
    u8 pendingIndexAfter;
    u8 forcedFlushReached;
    u8 historyTailCompacted;
    u8 mode2PatternScanRequested;
    u8 compactWindowCount;
    u8 compactWindow[OOT3D_TITLE_INTRO_RECORD_C_PATTERN_WINDOW_COUNT];
    u8 patternMatched;
    u8 matchedPatternIndex;
    u8 repeatedCompactWindowMatched;
    u8 globalPatternFlagSet;
    u8 activeClearRequested;
    u8 modeCleared;
} Oot3dTitleIntroRecordCHistoryCommitEvent;

typedef struct Oot3dTitleIntroRecordCFrameEvent {
    u8 progressionChecked;
    u8 progressionGatePassed;
    u8 stablePreviousSample;
    u8 currentSlotMissing;
    u8 recordByte0Written;
    u8 recordByte0Value;
    u8 cursorIncremented;
    u8 cursorWrappedToOne;
    u8 modeFFResetToZero;
    u8 recordCBefore[3];
    u8 recordCAfter[3];
    Oot3dTitleIntroRecordCHistoryCommitEvent historyCommit;
} Oot3dTitleIntroRecordCFrameEvent;

void Oot3d_TitleIntroRecordTableInit(
    Oot3dTitleIntroRecordTableState* table,
    Oot3dTitleIntroRecordCContext* context
);

void Oot3d_TitleIntroRecordCContextInit(
    Oot3dTitleIntroRecordCContext* context
);

Oot3dTitleIntroRecordCStatus Oot3d_TitleIntroRecordCFrameUpdate(
    Oot3dTitleIntroRecordCContext* context,
    Oot3dTitleIntroRecordTableState* table,
    Oot3dTitleIntroRecordCFrameEvent* outEvent
);
