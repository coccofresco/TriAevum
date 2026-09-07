#ifndef OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_H
#define OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_H

#include "oot3d/scene.h"

enum {
    OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX = 0xffff,
    OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_ROW_COUNT = 1,
    OOT3D_TITLE_INTRO_OPENING_REQUIRED_ASSET_REF_COUNT = 6,
    OOT3D_TITLE_INTRO_OPENING_NATIVE_COMMAND_REF_COUNT = 13,
    OOT3D_TITLE_INTRO_OPENING_PLAYER_ACTION_REF_COUNT = 15,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_CURVE_REF_COUNT = 6,
    OOT3D_TITLE_INTRO_OPENING_SCENE_ID = 0x6B,
    OOT3D_TITLE_INTRO_OPENING_SETUP_INDEX = 1,
    OOT3D_TITLE_INTRO_OPENING_CUTSCENE_SOURCE_INDEX = 94,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_BLOB_SEGMENT_SOURCE_INDEX = 452,
};

typedef struct {
    u16 orchestrationIndex;
    u8 sceneId;
    u16 setupIndex;
    u16 cutsceneSourceIndex;
    u32 nativeHeaderOffset;
    s32 nativeEndFrame;
    u16 nativeCommandRefStart;
    u16 nativeCommandRefCount;
    u16 nativeCommandLocalRefStart;
    u16 nativeCommandLocalRefCount;
    u16 playerActionRefStart;
    u16 playerActionRefCount;
    u16 playerActionMaxEndFrame;
    u16 cameraBlobSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraCmadRecordRefStart;
    u16 cameraCmadRecordRefCount;
    u16 cameraCurveRefStart;
    u16 cameraCurveRefCount;
    float slot6EyeDistanceToEmulator;
    u16 linkActorScaleIndex;
    u16 eponaActorScaleIndex;
    u16 linkActorAnimationIndex;
    u16 eponaActorAnimationIndex;
    u16 horseMotionAnimationIndex;
    u16 titleLogoActorInitIndex;
    u16 titleLogoComponentRefStart;
    u16 titleLogoComponentRefCount;
    u16 titleLogoDrawRefStart;
    u16 titleLogoDrawRefCount;
    u16 titleLogoUpdateRefStart;
    u16 titleLogoUpdateRefCount;
    const char* role;
    const char* scenePath;
    const char* slot6TracePath;
    const char* basis;
    const char* unresolved;
} Oot3dTitleIntroOpeningOrchestrationRow;

typedef struct {
    u16 assetRefIndex;
    u16 orchestrationIndex;
    u16 assetIndex;
    u8 required;
    u8 present;
    const char* assetRole;
    const char* romfsPath;
    const char* basis;
} Oot3dTitleIntroOpeningRequiredAssetRef;

typedef struct {
    u16 nativeCommandRefIndex;
    u16 orchestrationIndex;
    u16 nativeCommandSourceIndex;
    u16 localCommandIndex;
    u32 commandId;
    u16 entryCount;
    u32 blobSize;
    u32 commandOffset;
    const char* commandName;
    const char* category;
    const char* semanticKind;
} Oot3dTitleIntroOpeningNativeCommandRef;

typedef struct {
    u16 playerActionRefIndex;
    u16 orchestrationIndex;
    u16 playerActionSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 localCommandIndex;
    u16 entryIndex;
    u16 actionId;
    u16 startFrame;
    u16 endFrame;
    u16 durationFrames;
    const char* runtimeSemantic;
    const char* rawWordsText;
} Oot3dTitleIntroOpeningPlayerActionRef;

typedef struct {
    u16 cameraCurveRefIndex;
    u16 orchestrationIndex;
    u16 cameraCurveSourceIndex;
    u16 cmadRecordSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 nativeCommandSourceIndex;
    u8 channelType;
    u8 curveSlotIndex;
    u32 outputFieldOffset;
    u8 interpolationType;
    u16 pointCount;
    const char* curveRole;
} Oot3dTitleIntroOpeningCameraCurveRef;

extern const Oot3dTitleIntroOpeningOrchestrationRow gOot3dTitleIntroOpeningOrchestrationRows[];
extern const Oot3dTitleIntroOpeningRequiredAssetRef gOot3dTitleIntroOpeningRequiredAssetRefs[];
extern const Oot3dTitleIntroOpeningNativeCommandRef gOot3dTitleIntroOpeningNativeCommandRefs[];
extern const Oot3dTitleIntroOpeningPlayerActionRef gOot3dTitleIntroOpeningPlayerActionRefs[];
extern const Oot3dTitleIntroOpeningCameraCurveRef gOot3dTitleIntroOpeningCameraCurveRefs[];
extern const u32 gOot3dTitleIntroOpeningOrchestrationRowCount;
extern const u32 gOot3dTitleIntroOpeningRequiredAssetRefCount;
extern const u32 gOot3dTitleIntroOpeningNativeCommandRefCount;
extern const u32 gOot3dTitleIntroOpeningPlayerActionRefCount;
extern const u32 gOot3dTitleIntroOpeningCameraCurveRefCount;

const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningGetOrchestrationRow(u16 orchestrationIndex);
const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningFindOrchestrationRowForSceneCutscene(
    u8 sceneId,
    u16 setupIndex,
    u16 cutsceneSourceIndex
);
u16 Oot3d_TitleIntroOpeningFindOrchestrationIndexForSceneCutscene(
    u8 sceneId,
    u16 setupIndex,
    u16 cutsceneSourceIndex
);

#endif
