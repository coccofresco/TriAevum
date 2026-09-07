#ifndef OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_H

#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_camera_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/types.h"

typedef enum {
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK = 0,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NO_ACTIVE_SEGMENT,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_APPLY_FAILED,
    OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_PROJECT_FAILED,
} Oot3dTitleIntroOpeningCameraRuntimeStatus;

typedef struct Oot3dTitleIntroOpeningCameraReferenceRow {
    u16 referenceIndex;
    u16 orchestrationIndex;
    u16 setupIndex;
    u16 cutsceneSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraCmadRecordRefStart;
    u16 cameraCmadRecordRefCount;
    u16 cameraCurveRefStart;
    u16 cameraCurveRefCount;
    s32 segmentStartFrame;
    s32 segmentEndFrame;
    s32 sampleFrame;
    float slot6EyeX;
    float slot6EyeY;
    float slot6EyeZ;
    float slot6EyeDistanceToEmulator;
    const char* scenePath;
    const char* tracePath;
    const char* basis;
} Oot3dTitleIntroOpeningCameraReferenceRow;

typedef struct Oot3dTitleIntroOpeningCameraSample {
    float frame;
    u16 orchestrationIndex;
    u16 cameraBlobSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 introCameraTimelineSourceIndex;
    s32 segmentStartFrame;
    s32 segmentEndFrame;
    const Oot3dSceneCutsceneCameraBlobRow* cameraBlob;
    const Oot3dSceneCutsceneCameraBlobSegmentRow* cameraSegment;
    u8 hasSlot6Reference;
    float slot6ReferenceDistance;
    float slot6EyeDistanceToEmulator;
    Oot3dCutsceneCameraState csParams;
    Oot3dCutsceneCameraViewFrame view;
} Oot3dTitleIntroOpeningCameraSample;

extern const Oot3dTitleIntroOpeningCameraReferenceRow gOot3dTitleIntroOpeningCameraReferenceRows[];
extern const u32 gOot3dTitleIntroOpeningCameraReferenceRowCount;

const Oot3dTitleIntroOpeningCameraReferenceRow* Oot3d_TitleIntroOpeningCameraRuntimeGetReference(u16 referenceIndex);
const char* Oot3d_TitleIntroOpeningCameraRuntimeMissingOrchestrationStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeAppliedStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeOpeningFrameAppliedStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeSupersededByOpeningFrameStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeBlobBehavior(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneBehavior(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeOpeningFrameBehavior(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeMissingInitialSceneOrchestrationStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeMissingInitialSceneCutsceneStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneCutsceneMismatchStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneWindowCompleteStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneNoActiveRowStatus(void);
const char* Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneAppliedStatus(void);
Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildState(
    u16 orchestrationIndex,
    float frame,
    Oot3dCutsceneCameraState* outState
);
Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeBuildView(
    u16 orchestrationIndex,
    float frame,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dCutsceneCameraViewFrame* outView
);
Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSample(
    u16 orchestrationIndex,
    float frame,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dTitleIntroOpeningCameraSample* outSample
);
Oot3dTitleIntroOpeningCameraRuntimeStatus Oot3d_TitleIntroOpeningCameraRuntimeSampleSlot6Initial(
    Oot3dTitleIntroOpeningCameraSample* outSample
);

#endif
