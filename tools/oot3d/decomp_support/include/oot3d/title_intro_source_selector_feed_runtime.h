#pragma once

#include "oot3d/title_intro_source_selector_runtime.h"

#define OOT3D_TITLE_INTRO_FEED_CAPTURE_ENTRY 0x00422298u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_OWNER_ENTRY 0x00416F48u
#define OOT3D_TITLE_INTRO_FEED_GATE_CHANGE_ENTRY 0x003523DCu
#define OOT3D_TITLE_INTRO_FEED_FRAME_ADVANCE_ENTRY 0x00460878u
#define OOT3D_TITLE_INTRO_FEED_MODE_CHANGE_ENTRY 0x002D0264u
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_BIND_ENTRY 0x0033F248u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_INIT_ENTRY 0x0047AFB0u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_ADVANCE_ENTRY 0x0047BD30u
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_ADVANCE_ENTRY 0x00477A1Cu
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_CONTEXT_ADDRESS 0x005093E4u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_OFFSET 0x0014u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_ANALOG_SOURCE_OFFSET 0x0028u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_GLOBAL_MASK_OFFSET 0x002Cu
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_CAPTURE_ENABLE_OFFSET 0x0044u
#define OOT3D_TITLE_INTRO_FEED_GATE_SAVED_WORD_OFFSET 0x00D4u
#define OOT3D_TITLE_INTRO_FEED_LATCHED_VECTOR_X_OFFSET 0x004Au
#define OOT3D_TITLE_INTRO_FEED_LATCHED_VECTOR_Y_OFFSET 0x004Cu
#define OOT3D_TITLE_INTRO_FEED_ACTIVE_FLAG_OFFSET 0x0014u
#define OOT3D_TITLE_INTRO_FEED_GLOBAL_GATE_OFFSET 0x0015u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_PREVIOUS_SLOT_OFFSET 0x0019u
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_COUNTDOWN_OFFSET 0x001Du
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_LOOKUP_SOURCE_OFFSET 0x001Eu
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_SCALE_BYTE_OFFSET 0x001Fu
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_DISPATCH_BYTE_OFFSET 0x0020u
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_VECTOR_BYTE_OFFSET 0x0021u
#define OOT3D_TITLE_INTRO_FEED_COUNTDOWN_OVERRIDE_OFFSET 0x002Bu
#define OOT3D_TITLE_INTRO_FEED_REQUEST_MIN_COMPLETION_OFFSET 0x002Cu
#define OOT3D_TITLE_INTRO_FEED_REQUEST_HISTORY_COUNT_OFFSET 0x003Au
#define OOT3D_TITLE_INTRO_FEED_REQUEST_WINDOW_OPEN_OFFSET 0x003Bu
#define OOT3D_TITLE_INTRO_FEED_REQUEST_SCAN_INDEX_OFFSET 0x003Cu
#define OOT3D_TITLE_INTRO_FEED_REQUEST_SCAN_LIMIT_OFFSET 0x003Du
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_CURSOR_OFFSET 0x0042u
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_SLOT_SOURCE_OFFSET 0x0044u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_LATCHED_CODE_OFFSET 0x0046u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_ACTIVE_MASK_OFFSET 0x0050u
#define OOT3D_TITLE_INTRO_FEED_PREVIOUS_SLOT_OFFSET 0x0017u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_FLAGS_OFFSET 0x0094u
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_VECTOR_SCALE_OFFSET 0x009Cu
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_SCALE_BITS_OFFSET 0x00A0u
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_SOURCE_OFFSET 0x00A8u
#define OOT3D_TITLE_INTRO_FEED_TIME_COUNTER_OFFSET 0x00BCu
#define OOT3D_TITLE_INTRO_FEED_GATE_CURRENT_OFFSET 0x00DCu
#define OOT3D_TITLE_INTRO_FEED_GATE_STABLE_OFFSET 0x00E0u
#define OOT3D_TITLE_INTRO_FEED_GATE_PREVIOUS_OFFSET 0x00E4u
#define OOT3D_TITLE_INTRO_FEED_ACTIVE_SELECTOR_OFFSET 0x00E8u
#define OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MAX 0x40
#define OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MIN -0x40
#define OOT3D_TITLE_INTRO_FEED_MODE_OPEN_PREVIOUS_X 0x57u
#define OOT3D_TITLE_INTRO_FEED_TIME_INCREMENT 2u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_FLAG_ALT_HELPER 0x00004000u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_LOW_MASK 0x0000FFFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_SELECTED_FLAG 0x80000000u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_ACTIVE_MASK 0x3FFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_GLOBAL_PROMOTE 0x1000u
#define OOT3D_TITLE_INTRO_FEED_GLOBAL_FLAG_INACTIVE 0xFFu
#define OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_MULTIPLIER 0x00100000u
#define OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_COMPARE 0xFFF00000u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_SENTINEL 0xFFFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_CFFF 0xCFFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_0FFF 0x0FFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_CFFF_OVERRIDE 0xDFFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_0FFF_OVERRIDE 0x1FFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_LIMIT_DEFAULT 0x0Du
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_LIMIT_A000 0x0Eu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_MASK_A000 0xA000u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_HISTORY_COPY_MASK 0xD000u
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_MIN_COMPLETION_DEFAULT 8u
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_LOOKUP_EMPTY 0xFFu
#define OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT
#define OOT3D_TITLE_INTRO_FEED_REQUEST_PROGRESS_STEP 0x12u
#define OOT3D_TITLE_INTRO_FEED_REQUEST_COMPLETION_WINDOW 10u
#define OOT3D_TITLE_INTRO_FEED_SEQUENCE_INACTIVE_DELTA 3u
#define OOT3D_TITLE_INTRO_FEED_SCALE_BYTE_FACTOR_BITS 0x3C010204u
#define OOT3D_TITLE_INTRO_FEED_RECORD_A_PENDING_RUNTIME_BYTE 0xFEu
#define OOT3D_TITLE_INTRO_FEED_RECORD_B_DEFAULT_SEQUENCE_MODULO 8u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_OWNER_SCENE_MODE_ACTIVE 2u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_DISABLED 5u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_RETURN_3_A 2u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_RETURN_3_B 0x0Fu
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_RETURN_1_A 0x10u
#define OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_RETURN_1_B 1u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_PRIMARY 0x00000001u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT11 0x00000400u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT9 0x00000800u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT2 0x00000200u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT5 0x00000100u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_HIGH 0x00000040u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_LOW 0x00000080u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_HIGH_GROUP 0x00000050u
#define OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_LOW_GROUP 0x000000A0u

typedef enum {
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_OK = 0,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_STATE,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_SELECTOR,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_RECORD_CONTEXT,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_RECORD_TABLE,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_SEQUENCE_SOURCE,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_CAPTURE_INPUT,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_SELECTOR_APPLY_FAILED,
} Oot3dTitleIntroSourceSelectorFeedStatus;

typedef struct Oot3dTitleIntroRuntimeRequestConfig {
    u8 globalFlagResolved;
    s8 globalFlagValue;
    u8 promoteGlobalFlagBit;
    u8 overrideCfffResolved;
    u16 overrideCfff;
    u8 override0fffResolved;
    u16 override0fff;
} Oot3dTitleIntroRuntimeRequestConfig;

typedef struct Oot3dTitleIntroGateCaptureInput {
    u8 sceneInputMode;
    u8 gateCaptureEnabled;
    u8 analogSourceEnabled;
    u8 clearPrimaryBit;
    u32 inputMask;
    u32 analogInputMask;
    s16 analogVectorX;
    s16 analogVectorY;
} Oot3dTitleIntroGateCaptureInput;

typedef struct Oot3dTitleIntroNativeInputContext {
    u32 modeDiscriminator;
    u32 analogSourceValue;
    u32 globalInputMask;
    u32 laneInputMask;
    u32 gateCaptureEnable;
    u8 clearPrimaryBit;
    s16 analogVectorX;
    s16 analogVectorY;
} Oot3dTitleIntroNativeInputContext;

typedef struct Oot3dTitleIntroSourceSelectorFeedState {
    u32 gateSavedWordSource;
    s16 latchedVectorX;
    s16 latchedVectorY;
    u32 closedStableGateWord;
    u8 activeFlag;
    u32 timeCounter;
    u8 countdownOverride;
    u8 requestMinCompletionIndex;
    u8 requestHistoryCount;
    u8 requestWindowOpen;
    u8 requestPreviousSlot;
    u8 requestScanIndex;
    u8 requestScanLimit;
    u16 requestLatchedCode;
    u16 requestActiveMask;
    u16 requestCursorBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT];
    u16 requestPreviousProgressBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT];
    u16 requestDurationBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT];
    u8 requestLookupBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT];
    const Oot3dTitleIntroSequenceRow* sequenceRows;
    u32 sequenceRowCount;
    u16 sequenceCursor;
    u16 sequenceSlotSource;
    u32 sequenceLastProgressTime;
    u32 sequenceStateWord;
    u8 recordBLookupSource;
    u8 recordBScaleByte;
    s8 recordBDispatchByte;
    s8 recordBVectorByte;
    u32 recordBScaleBits;
    u32 recordBVectorScaleBits;
} Oot3dTitleIntroSourceSelectorFeedState;

typedef struct Oot3dTitleIntroGateCaptureEvent {
    u8 applied;
    u8 sceneModeActive;
    u8 gateCaptureEnabled;
    u8 analogSourceEnabled;
    u8 clearPrimaryBitApplied;
    u8 analogHighGroupApplied;
    u8 analogLowGroupApplied;
    u8 analogDirectVectorApplied;
    u32 inputMask;
    u32 analogInputMask;
    u32 capturedGateWord;
    s16 capturedVectorX;
    s16 capturedVectorY;
} Oot3dTitleIntroGateCaptureEvent;

typedef struct Oot3dTitleIntroNativeInputCaptureEvent {
    u8 applied;
    u8 analogSourceEnabled;
    u8 globalMaskUsed;
    u8 laneMaskUsed;
    u32 nativeInputContextAddress;
    u32 modeDiscriminator;
    u32 analogSourceValue;
    u32 globalInputMask;
    u32 laneInputMask;
    u32 gateCaptureEnable;
    u8 sceneInputMode;
    Oot3dTitleIntroGateCaptureEvent gateCaptureEvent;
} Oot3dTitleIntroNativeInputCaptureEvent;

typedef struct Oot3dTitleIntroSourceSelectorFeedEvent {
    u8 capturedGateAndVector;
    u32 capturedGateWord;
    s16 capturedVectorX;
    s16 capturedVectorY;
    u8 gateChanged;
    u8 gateOpened;
    u8 gateClosed;
    u8 gateLatchedFromSaved;
    u8 vectorsClamped;
    s8 clampedVectorX;
    s8 clampedVectorY;
    u8 selectorApplied;
    Oot3dTitleIntroSourceSelectorStatus selectorStatus;
    Oot3dTitleIntroSourceSelectorEvent selectorEvent;
    u8 modeChanged;
    u8 modeOpened;
    u8 modeClosed;
    u8 modeCloseCommitRequested;
    u8 currentProgressTimeSnapshotted;
    u32 timeCounterBefore;
    u32 timeCounterAfter;
    u8 frameGateRolledFromSaved;
    u8 runtimeRequestHelperPending;
    u8 runtimeRequestAltHelperSelected;
    u8 dirtySetOnSlotChange;
    u8 previousSlotLatched;
    u8 sequenceBound;
    u8 sequenceCleared;
    u8 sequenceLeadingSentinelSkipped;
    u16 sequenceCursor;
    u8 runtimeRequestInitialized;
    u8 runtimeRequestCleared;
    u8 runtimeRequestPatternMatcherPending;
    u8 runtimeRequestHistoryCopyPending;
    u16 runtimeRequestSelectedMask;
    u8 runtimeRequestScanLimit;
    u16 runtimeRequestActiveMask;
    u8 runtimeRequestAdvanceApplied;
    u8 runtimeRequestWindowOpened;
    u8 runtimeRequestSlotChanged;
    u8 runtimeRequestCompleted;
    u8 runtimeRequestAllMasksCleared;
    u8 runtimeRequestLatchedCodeSet;
    u8 runtimeRequestClearedByBounds;
    u8 runtimeRequestLaneAdvanced;
    u8 runtimeRequestLaneCleared;
    u8 runtimeRequestCountdownOverride;
    u8 nativeSequenceBound;
    u8 nativeSequenceParam;
    u8 nativeSequenceLaneIndex;
    u8 sequenceAdvanceApplied;
    u8 sequenceTimerUpdated;
    u8 sequenceRowLoaded;
    u8 sequenceCountdownDecremented;
    u8 sequenceCompleted;
    u8 sequenceSlotAdvanced;
    u8 recordBScaleChanged;
    u8 recordBDispatchChanged;
    u8 recordBVectorChanged;
    u8 recordBLookupChanged;
    u8 recordBDispatchSuppressedByGate;
    u16 sequenceRowIndex;
    u32 sequenceTimer;
    u8 recordAComposed;
    u8 recordAByte1FromCountdownOverride;
    u8 recordBComposed;
    u8 recordBCompactLookupApplied;
    u8 recordBCompactLookupSpecial5Resolved;
    u8 recordBDefaultSequenceModuloApplied;
    u8 recordABytes[3];
    u8 recordBBytes[3];
    Oot3dTitleIntroDispatchEvent recordBDispatchEvent;
    Oot3dTitleIntroDispatchEvent recordBPayloadDispatchEvent;
    Oot3dTitleIntroDispatchEvent recordBScaleDispatchEvent;
    Oot3dTitleIntroDispatchEvent recordBSlotDispatchEvent;
    Oot3dTitleIntroDispatchEvent recordBLookupDispatchEvent;
    Oot3dTitleIntroDispatchEvent recordBVectorResetDispatchEvent;
} Oot3dTitleIntroSourceSelectorFeedEvent;

void Oot3d_TitleIntroSourceSelectorFeedInit(
    Oot3dTitleIntroSourceSelectorFeedState* state
);

void Oot3d_TitleIntroSourceSelectorFeedCaptureGateAndVector(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    u32 gateWord,
    s16 vectorX,
    s16 vectorY,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInput(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    const Oot3dTitleIntroGateCaptureInput* input,
    Oot3dTitleIntroGateCaptureEvent* outEvent
);

u8 Oot3d_TitleIntroSourceSelectorFeedNativeInputSceneMode(u32 modeDiscriminator);

u8 Oot3d_TitleIntroSourceSelectorFeedNativeInputAnalogSourceEnabled(
    const Oot3dTitleIntroNativeInputContext* inputContext
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInputContext(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    const Oot3dTitleIntroNativeInputContext* inputContext,
    Oot3dTitleIntroNativeInputCaptureEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedApplyGlobalGate(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroSourceSelectorState* selector,
    Oot3dTitleIntroRecordCContext* recordContext,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    u8 globalGateByte,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedSetMode(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroRecordCContext* recordContext,
    u8 modeByte,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedBindSequence(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroRecordCContext* recordContext,
    const Oot3dTitleIntroSequenceRow* rows,
    u32 rowCount,
    u8 recordBCountdown,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedBindNativeSequence(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroRecordCContext* recordContext,
    u8 nativeParam,
    u8 recordBCountdown,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedComposeRecordTable(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroSourceSelectorState* selector,
    Oot3dTitleIntroRecordCContext* recordContext,
    Oot3dTitleIntroRecordTableState* table,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedInitializeRuntimeRequest(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroSourceSelectorState* selector,
    Oot3dTitleIntroRecordCContext* recordContext,
    const Oot3dTitleIntroRuntimeRequestConfig* config,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);

Oot3dTitleIntroSourceSelectorFeedStatus Oot3d_TitleIntroSourceSelectorFeedFrameAdvance(
    Oot3dTitleIntroSourceSelectorFeedState* state,
    Oot3dTitleIntroSourceSelectorState* selector,
    Oot3dTitleIntroRecordCContext* recordContext,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    Oot3dTitleIntroSourceSelectorFeedEvent* outEvent
);
