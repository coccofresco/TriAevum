#pragma once

#include <stdint.h>

#define OOT3D_TITLE_INTRO_RUNTIME_CONTEXT_ADDR 0x0054AC48u
#define OOT3D_TITLE_INTRO_RECORD_TABLE_ADDR 0x0054AC9Du
#define OOT3D_TITLE_INTRO_RECORD_C_ADDR 0x0054ACA3u
#define OOT3D_TITLE_INTRO_HISTORY_OTHER_MODES_ADDR 0x0054BE0Au
#define OOT3D_TITLE_INTRO_HISTORY_MODE1_ADDR 0x0054BE12u
#define OOT3D_TITLE_INTRO_COMPACT_LOOKUP_ADDR 0x0054C212u
#define OOT3D_TITLE_INTRO_PATTERN_RECORDS_ADDR 0x0054C222u
#define OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_ADDR 0x0054C28Fu
#define OOT3D_TITLE_INTRO_VECTOR_LOOKUP_ADDR 0x0054C2A0u
#define OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_ADDR 0x0054AC96u
#define OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_ADDR 0x0054B5F2u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_TABLE_ADDR 0x0054B5F2u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_TABLE_STRIDE 0xA0u

#define OOT3D_TITLE_INTRO_COMPACT_LOOKUP_COUNT 16u
#define OOT3D_TITLE_INTRO_PATTERN_RECORD_COUNT 12u
#define OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_COUNT 8u
#define OOT3D_TITLE_INTRO_VECTOR_LOOKUP_COUNT 256u
#define OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_COUNT 16u
#define OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_PREFIX_COUNT 32u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT 15u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_ROW_COUNT 81u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_DEFAULT_LANE 14u

typedef struct Oot3dTitleIntroHistoryRecord {
    uint8_t previousSlot;
    uint8_t paddingOrUnknown;
    int16_t durationDelta;
    uint8_t previousX;
    uint8_t previousY;
    uint8_t previousZ;
    uint8_t sourceFlagsHi2;
} Oot3dTitleIntroHistoryRecord;

typedef struct Oot3dTitleIntroPatternRecord {
    uint8_t length;
    uint8_t values[8];
} Oot3dTitleIntroPatternRecord;

typedef struct Oot3dTitleIntroSequenceRow {
    int8_t sourceByte;
    uint8_t unusedOrPadding;
    uint16_t duration;
    uint8_t scaleByte;
    int8_t dispatchByte;
    int8_t vectorY;
    int8_t alternateSourceByte;
} Oot3dTitleIntroSequenceRow;

typedef struct Oot3dTitleIntroSequenceLane {
    uint8_t laneIndex;
    uint8_t param1Value;
    uint8_t defaultForParamGe15;
    uint8_t reserved;
    uint16_t rowStart;
    uint16_t rowCount;
    uint32_t nativeAddress;
} Oot3dTitleIntroSequenceLane;

typedef struct Oot3dTitleIntroRuntimeTableMapRow {
    const char* symbol;
    uint32_t nativeAddress;
    uint32_t byteSize;
    const char* source;
} Oot3dTitleIntroRuntimeTableMapRow;

extern const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryOtherModeRecords[];
extern const uint32_t gOot3dTitleIntroHistoryOtherModeRecordCount;
extern const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryMode1Records[];
extern const uint32_t gOot3dTitleIntroHistoryMode1RecordCount;
extern const uint8_t gOot3dTitleIntroCompactLookup[OOT3D_TITLE_INTRO_COMPACT_LOOKUP_COUNT];
extern const Oot3dTitleIntroPatternRecord gOot3dTitleIntroPatternRecords[OOT3D_TITLE_INTRO_PATTERN_RECORD_COUNT];
extern const uint8_t gOot3dTitleIntroFallbackZeroRun[OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_COUNT];
extern const uint32_t gOot3dTitleIntroVectorLookupBits[OOT3D_TITLE_INTRO_VECTOR_LOOKUP_COUNT];
extern const uint8_t gOot3dTitleIntroInputVectorPrefix[OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_COUNT];
extern const uint8_t gOot3dTitleIntroGlobalFlagContextProbePrefix[OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_PREFIX_COUNT];
extern const Oot3dTitleIntroSequenceRow gOot3dTitleIntroDeferredSequenceRows[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_ROW_COUNT];
extern const Oot3dTitleIntroSequenceLane gOot3dTitleIntroDeferredSequenceLanes[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT];
extern const Oot3dTitleIntroRuntimeTableMapRow gOot3dTitleIntroRuntimeTableMapRows[];
extern const uint32_t gOot3dTitleIntroRuntimeTableMapRowCount;
