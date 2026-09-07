#pragma once

#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_RECORD_BYTE_TABLE_ADDR 0x005A2E7Cu
#define OOT3D_TITLE_INTRO_FIRST_CHANGE_LATCH_ADDR 0x0055A21Cu
#define OOT3D_TITLE_INTRO_FIRST_CHANGE_INIT_CONTEXT_ADDR 0x005BE5B8u
#define OOT3D_TITLE_INTRO_RECORD_CHANGE_NOTIFY_CONTEXT_ADDR 0x005C1878u
#define OOT3D_TITLE_INTRO_DISPATCH_CONTEXT_ADDR 0x005B12E8u
#define OOT3D_TITLE_INTRO_DISPATCH_GATE_CONTEXT_ADDR 0x0054AC25u
#define OOT3D_TITLE_INTRO_RECORD_APPLY_ENTRY 0x002D038Cu
#define OOT3D_TITLE_INTRO_DISPATCH_CODE5_ENTRY 0x002CFCF0u
#define OOT3D_TITLE_INTRO_DISPATCH_CODE7_SCALE_ENTRY 0x002CFD24u
#define OOT3D_TITLE_INTRO_DISPATCH_PAYLOAD_REBIND_ENTRY 0x002CFD74u
#define OOT3D_TITLE_INTRO_DISPATCH_CODE6_ENTRY 0x002CFE00u

#define OOT3D_TITLE_INTRO_RECORD_BYTE_SIDE_EFFECT_LIMIT 8u
#define OOT3D_TITLE_INTRO_DISPATCH_GATE_BYTE_OFFSET 0x0005u
#define OOT3D_TITLE_INTRO_DISPATCH_GATE_OPEN_VALUE 0u
#define OOT3D_TITLE_INTRO_DISPATCH_CODE_SLOT 5u
#define OOT3D_TITLE_INTRO_DISPATCH_CODE_VECTOR 6u
#define OOT3D_TITLE_INTRO_DISPATCH_CODE_SCALE 7u
#define OOT3D_TITLE_INTRO_DISPATCH_SCALE_NONZERO_BITS 0x3F800000u
#define OOT3D_TITLE_INTRO_DISPATCH_SCALE_ZERO_BITS 0x3FB33333u
#define OOT3D_TITLE_INTRO_PAYLOAD_DESCRIPTOR 0x010004E0u
#define OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_SOURCE_ADDR 0x0054ACE4u
#define OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_LITERAL_ADDR 0x00477C8Cu
#define OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_VALUE_BITS 0x3F800000u
#define OOT3D_TITLE_INTRO_DIRECT_STATE_37 0x25u
#define OOT3D_TITLE_INTRO_DIRECT_STATE_38 0x26u
#define OOT3D_TITLE_INTRO_DIRECT_STATE_39 0x27u
#define OOT3D_TITLE_INTRO_STATE37_PLAYER_BYTE_OFFSET 0x0300u
#define OOT3D_TITLE_INTRO_STATE37_PLAYER_TIMER_OFFSET 0x02D3u
#define OOT3D_TITLE_INTRO_STATE37_PLAYER_TIMER_VALUE 0x1Eu
#define OOT3D_TITLE_INTRO_STATE37_ADVANCE_MESSAGE_STATE 2u
#define OOT3D_TITLE_INTRO_STATE37_ADVANCE_ACTOR_GATE_KIND 5u
#define OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_PTR_LITERAL_ADDR 0x00474E24u
#define OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_SOURCE_ADDR 0x004FC654u
#define OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_DEFAULT_VALUE 0x00000000u
#define OOT3D_TITLE_INTRO_STATE37_PLAYER_FLAGS_OFFSET 0x0018u
#define OOT3D_TITLE_INTRO_STATE37_FALLBACK_BYTE_OFFSET 0x6016u

typedef enum {
    OOT3D_TITLE_INTRO_PLAYBACK_OK = 0,
    OOT3D_TITLE_INTRO_PLAYBACK_NULL_STATE,
    OOT3D_TITLE_INTRO_PLAYBACK_NULL_TABLE,
    OOT3D_TITLE_INTRO_PLAYBACK_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_PLAYBACK_INDEX_OUT_OF_RANGE,
    OOT3D_TITLE_INTRO_PLAYBACK_NULL_PAYLOAD,
    OOT3D_TITLE_INTRO_PLAYBACK_NULL_RECORD,
    OOT3D_TITLE_INTRO_PLAYBACK_UNINITIALIZED,
} Oot3dTitleIntroPlaybackStatus;

typedef void (*Oot3dTitleIntroFirstChangeInitFunc)(void* user, u32 context);
typedef void (*Oot3dTitleIntroRecordChangeNotifyFunc)(void* user, u32 context, u32 index, u8 value);
typedef void (*Oot3dTitleIntroDispatchCodeFunc)(void* user, u32 dispatchContext, u32 code);
typedef void (*Oot3dTitleIntroPayloadRebindFunc)(void* user, u32 descriptor, u32 payloadValue, s32 payloadArg);

typedef struct Oot3dTitleIntroPlaybackHooks {
    void* user;
    Oot3dTitleIntroFirstChangeInitFunc firstChangeInit;
    Oot3dTitleIntroRecordChangeNotifyFunc recordChangeNotify;
    Oot3dTitleIntroDispatchCodeFunc dispatchCode;
    Oot3dTitleIntroPayloadRebindFunc payloadRebind;
} Oot3dTitleIntroPlaybackHooks;

typedef struct Oot3dTitleIntroRecordByteTableState {
    u8* bytes;
    u32 byteCount;
    u32 firstChangeLatch;
    u32 firstChangeInitCount;
    u32 notifyCount;
    u32 writeCount;
} Oot3dTitleIntroRecordByteTableState;

typedef struct Oot3dTitleIntroRecordByteApplyEvent {
    u8 changed;
    u8 oldValue;
    u8 newValue;
    u32 index;
    u8 firstChangeInitRequested;
    u8 notifyRequested;
    u8 storedAfterNotify;
} Oot3dTitleIntroRecordByteApplyEvent;

typedef struct Oot3dTitleIntroDispatchState {
    u32 dispatchContext;
    u8 dispatchGateByte5;
    u8 dispatchHandlePresent;
    u8 payloadAllocatorAvailable;
    u32 lastDispatchCode;
    u32 dispatchCount;
    u32 scaleBits;
    u32 payloadDescriptor;
    u32 payloadValue;
    s32 payloadArg;
    u32 payloadHandleB8Value;
    u8 payloadBound;
} Oot3dTitleIntroDispatchState;

typedef struct Oot3dTitleIntroDispatchEvent {
    u8 dispatched;
    u32 code;
    u32 scaleBits;
    u8 payloadRebindRequested;
    u8 payloadClearRequested;
    u8 payloadHandleB8Written;
    u8 payloadActivated;
    u32 payloadDescriptor;
    u32 payloadValue;
    s32 payloadArg;
} Oot3dTitleIntroDispatchEvent;

typedef struct Oot3dTitleIntroState37PlaybackState {
    const u8* recordC;
    s16 cursor;
    u8 playerByte300;
    u8 playerTimer2D3;
    u8 stateChangeRequested;
    u8 nextDirectState;
    u32 stepCount;
} Oot3dTitleIntroState37PlaybackState;

typedef struct Oot3dTitleIntroState37ExternalAdvanceGateInput {
    u8 messageState;
    u8 actorGate5Passed;
    u32 playerFlags18;
    u32 requiredFlagMask;
    u8 fallbackByte6016;
} Oot3dTitleIntroState37ExternalAdvanceGateInput;

typedef struct Oot3dTitleIntroState37ExternalAdvanceGateEvent {
    u8 messageStateMatched;
    u8 actorGate5Matched;
    u8 playerFlagMaskPassed;
    u8 fallbackBytePassed;
    u8 passed;
} Oot3dTitleIntroState37ExternalAdvanceGateEvent;

typedef struct Oot3dTitleIntroState37Event {
    u8 recordByte0;
    u8 recordModeByte1;
    u8 recordCursorByte2;
    u8 recordReadyForCursor;
    u8 playerByte300Written;
    u8 playerByte300Value;
    s16 cursorBefore;
    s16 cursorAfter;
    u8 cursorIncremented;
    Oot3dTitleIntroRecordByteApplyEvent currentApply;
    Oot3dTitleIntroRecordByteApplyEvent terminatorApply;
    Oot3dTitleIntroState37ExternalAdvanceGateEvent externalAdvanceGate;
    u8 requestPlayerTimer2D3;
    u8 requestModeResetToZero;
    u8 requestState38;
    u8 requestState39;
    u8 blockedOnExternalAdvanceGate;
} Oot3dTitleIntroState37Event;

void Oot3d_TitleIntroRecordByteTableInit(
    Oot3dTitleIntroRecordByteTableState* state,
    u8* bytes,
    u32 byteCount
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroApplyRecordByte(
    Oot3dTitleIntroRecordByteTableState* state,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    u32 index,
    u8 value,
    Oot3dTitleIntroRecordByteApplyEvent* outEvent
);

void Oot3d_TitleIntroDispatchStateInit(
    Oot3dTitleIntroDispatchState* state,
    u8 dispatchHandlePresent,
    u8 payloadAllocatorAvailable
);

u8 Oot3d_TitleIntroDispatchGateOpen(
    const Oot3dTitleIntroDispatchState* state
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroDispatchCode5(
    Oot3dTitleIntroDispatchState* state,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    Oot3dTitleIntroDispatchEvent* outEvent
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroDispatchCode6(
    Oot3dTitleIntroDispatchState* state,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    Oot3dTitleIntroDispatchEvent* outEvent
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroDispatchCode7AndScale(
    Oot3dTitleIntroDispatchState* state,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    u8 selectorNonZero,
    Oot3dTitleIntroDispatchEvent* outEvent
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroRebindPayload(
    Oot3dTitleIntroDispatchState* state,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const u32* payloadValue,
    s32 payloadArg,
    Oot3dTitleIntroDispatchEvent* outEvent
);

void Oot3d_TitleIntroState37Init(
    Oot3dTitleIntroState37PlaybackState* state,
    s16 cursor
);

u8 Oot3d_TitleIntroState37ExternalAdvanceGatePassed(
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* input,
    Oot3dTitleIntroState37ExternalAdvanceGateEvent* outEvent
);

Oot3dTitleIntroPlaybackStatus Oot3d_TitleIntroState37Step(
    Oot3dTitleIntroState37PlaybackState* state,
    Oot3dTitleIntroRecordByteTableState* recordByteTable,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    const u8 recordC[3],
    const Oot3dTitleIntroState37ExternalAdvanceGateInput* externalAdvanceGate,
    Oot3dTitleIntroState37Event* outEvent
);
