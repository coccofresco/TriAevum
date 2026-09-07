#ifndef OOT3D_TITLE_INTRO_OPENING_PLAYER_MOTION_H
#define OOT3D_TITLE_INTRO_OPENING_PLAYER_MOTION_H

#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CONSUMER_ENTRY 0x001A35CCu
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT 12u
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_X_OFFSET 0x28u
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_Z_OFFSET 0x2Cu
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_X_WORD_INDEX 10u
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_Z_WORD_INDEX 11u
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_BITS 0x42700000u
#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_VALUE 60.0f

typedef enum {
    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK = 0,
    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD,
    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_OUTPUT,
} Oot3dTitleIntroPlayerActionMotionStatus;

typedef s16 (*Oot3dTitleIntroMathAtan2SFunc)(float arg0Z, float arg1X, void* user);

typedef struct {
    u32 rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT];
} Oot3dTitleIntroPlayerActionMotionRecord;

typedef struct {
    u16 actionId;
    u16 startFrame;
    u16 endFrame;
    u16 durationFrames;
    s16 rotX;
    s16 rotY;
    s16 rotZ;
    s32 startX;
    s32 startY;
    s32 startZ;
    s32 endX;
    s32 endY;
    s32 endZ;
    float tailWord9F32;
    float tailWord10F32;
    float tailWord11F32;
} Oot3dTitleIntroPlayerActionTransform;

typedef struct {
    u16 motionRefIndex;
    u16 orchestrationIndex;
    u16 playerActionRefIndex;
    u16 playerActionSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u16 actionId;
    u16 startFrame;
    u16 endFrame;
    u16 durationFrames;
    u8 directStateCasePresent;
    u8 notDirectStateCase;
    u8 hasQuantizedMotionVector;
    u8 speedClamped;
    u32 vectorXBits;
    u32 vectorZBits;
    float vectorXF32;
    float vectorZF32;
    s32 vectorXS32Vcvt;
    s32 vectorZS32Vcvt;
    float vectorXQuantizedF32;
    float vectorZQuantizedF32;
    float unclampedSpeed;
    float clampedSpeed;
    float atan2Arg0Z;
    float atan2Arg1NegX;
    s16 nativeHeadingS16;
    u16 nativeHeadingU16;
    const char* actionIdHex;
    const char* rawWordsText;
    u32 rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT];
} Oot3dTitleIntroOpeningPlayerMotionRow;

typedef struct {
    float vectorXF32;
    float vectorZF32;
    s32 vectorXS32Vcvt;
    s32 vectorZS32Vcvt;
    float vectorXQuantizedF32;
    float vectorZQuantizedF32;
    float unclampedSpeed;
    float clampedSpeed;
    u8 speedClamped;
    u8 headingComputed;
    float atan2Arg0Z;
    float atan2Arg1NegX;
    s16 heading;
} Oot3dTitleIntroPlayerActionMotionResult;

extern const Oot3dTitleIntroOpeningPlayerMotionRow gOot3dTitleIntroOpeningPlayerMotionRows[15];
extern const u32 gOot3dTitleIntroOpeningPlayerMotionRowCount;

const Oot3dTitleIntroOpeningPlayerMotionRow* Oot3d_TitleIntroOpeningGetPlayerMotionRow(u16 motionRefIndex);
Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionComputeMotion(
    const Oot3dTitleIntroPlayerActionMotionRecord* record,
    Oot3dTitleIntroMathAtan2SFunc atan2S,
    void* atan2SUser,
    Oot3dTitleIntroPlayerActionMotionResult* outResult
);
Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionDecodeTransform(
    const Oot3dTitleIntroPlayerActionMotionRecord* record,
    Oot3dTitleIntroPlayerActionTransform* outTransform
);
Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningComputePlayerMotionRow(
    const Oot3dTitleIntroOpeningPlayerMotionRow* row,
    Oot3dTitleIntroMathAtan2SFunc atan2S,
    void* atan2SUser,
    Oot3dTitleIntroPlayerActionMotionResult* outResult
);
Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningDecodePlayerMotionTransform(
    const Oot3dTitleIntroOpeningPlayerMotionRow* row,
    Oot3dTitleIntroPlayerActionTransform* outTransform
);

#endif
