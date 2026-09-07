#pragma once

#include "oot3d/title_intro_playback_runtime.h"
#include "oot3d/title_intro_record_c_runtime.h"
#include "oot3d/title_intro_runtime_tables.h"

#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ENTRY 0x002D6798u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MASK 0x0000FFFCu
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK 0x00000F80u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT2 0x00000080u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT5 0x00000200u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT9 0x00000800u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT11 0x00000400u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT14 0x00000100u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_HIGH 0x00000002u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_LOW 0x00000001u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_SOURCE_HIGH_ADJUST 0x80u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_SOURCE_LOW_ADJUST 0x40u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MODE2_VECTOR_BITS 0x3F800000u
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_INDEX_BIAS 128
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_POSITIVE_LIMIT 64
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_NEGATIVE_LIMIT -64
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_POSITIVE_SCALE 127
#define OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_NEGATIVE_SCALE 128

typedef enum {
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_OK = 0,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_NULL_STATE,
    OOT3D_TITLE_INTRO_SOURCE_SELECTOR_NULL_RECORD_CONTEXT,
} Oot3dTitleIntroSourceSelectorStatus;

typedef struct Oot3dTitleIntroSourceSelectorState {
    u32 gateCurrentWord;
    u32 gateStableWord;
    u32 gatePreviousWord;
    u32 activeSelectorWord;
    u32 runtimeFlagsOrRequest;
    u8 dirtyCountdown;
    u8 latchedGateFlag;
    u8 globalGateByte;
    u8 previousSlotOrId;
    s8 inputVectorX;
    s8 inputVectorY;
    u32 vectorScaleBits;
    u32 payloadValue;
    s32 payloadArg;
    u8 payloadValueResolved;
} Oot3dTitleIntroSourceSelectorState;

typedef struct Oot3dTitleIntroSourceSelectorEvent {
    u8 dirtyCountdownShortCircuit;
    u8 stableGateShortCircuit;
    u32 maskedGateWord;
    u32 newlyActiveBits;
    u32 activeSelectorWord;
    u8 selected;
    u32 selectedBit;
    u8 selectedSlot;
    u8 selectedSourceByte;
    u8 highAdjustApplied;
    u8 lowAdjustApplied;
    u8 mode2VectorPath;
    u8 normalVectorPath;
    u8 vectorLookupIndex;
    u8 vectorDispatchValue;
    u32 vectorScaleBits;
    u8 dispatchSlotChanged;
    u8 slotClearRequested;
    u8 payloadRebindSkipped;
    Oot3dTitleIntroDispatchEvent dispatchScaleEvent;
    Oot3dTitleIntroDispatchEvent dispatchPayloadEvent;
    Oot3dTitleIntroDispatchEvent dispatchSlotEvent;
    Oot3dTitleIntroDispatchEvent dispatchVectorEvent;
    Oot3dTitleIntroDispatchEvent dispatchVectorResetEvent;
} Oot3dTitleIntroSourceSelectorEvent;

void Oot3d_TitleIntroSourceSelectorInit(
    Oot3dTitleIntroSourceSelectorState* state
);

Oot3dTitleIntroSourceSelectorStatus Oot3d_TitleIntroSourceSelectorApply(
    Oot3dTitleIntroSourceSelectorState* state,
    Oot3dTitleIntroRecordCContext* recordContext,
    Oot3dTitleIntroDispatchState* dispatchState,
    const Oot3dTitleIntroPlaybackHooks* hooks,
    Oot3dTitleIntroSourceSelectorEvent* outEvent
);
