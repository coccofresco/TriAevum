#ifndef OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_H
#define OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_H

#include "oot3d/scene_cutscene_camera_runtime.h"

enum {
    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_TIMELINE_ROW_COUNT = 119,
    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_CUTSCENE_ROW_COUNT = 22,
    OOT3D_SCENE_CUTSCENE_INTRO_CAMERA_NO_STRT_LABEL = 0xFFFF,
};

typedef struct {
    u16 introCameraTimelineSourceIndex;
    u16 cutsceneSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u8 sceneId;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 segmentIndex;
    s32 startFrame;
    s32 endFrame;
    s32 sampleFrame;
    u16 firstCmadRecordSourceIndex;
    u16 cmadRecordCount;
    u16 curveCount;
    u16 keyframeCount;
    u16 strtSourceIndex;
    u16 strtLabelSourceIndex;
    u16 strtParsedCameraIndex;
    s32 strtParsedStartFrame;
    s32 strtParsedEndFrame;
    const char* scenePath;
    const char* strtLabel;
    const char* strtCurveRole;
} Oot3dSceneCutsceneIntroCameraTimelineRow;

typedef struct {
    u16 cutsceneSourceIndex;
    u16 cameraTimelineRefStart;
    u16 cameraTimelineRefCount;
    u16 miscActionCount;
    u16 setupIndex;
    s32 cameraFrameStart;
    s32 cameraFrameEnd;
    const char* scenePath;
} Oot3dSceneCutsceneIntroCameraCutsceneRow;

typedef enum {
    OOT3D_CUTSCENE_INTRO_CAMERA_OK = 0,
    OOT3D_CUTSCENE_INTRO_CAMERA_NULL_VIEW,
    OOT3D_CUTSCENE_INTRO_CAMERA_NO_ACTIVE_TIMELINE_ROW,
    OOT3D_CUTSCENE_INTRO_CAMERA_RUNTIME_FAILED,
} Oot3dCutsceneIntroCameraStatus;

extern const Oot3dSceneCutsceneIntroCameraTimelineRow oot3d_scene_cutscene_intro_camera_timeline_rows[];
extern const Oot3dSceneCutsceneIntroCameraCutsceneRow oot3d_scene_cutscene_intro_camera_cutscene_rows[];
extern const u32 oot3d_scene_cutscene_intro_camera_timeline_row_count;
extern const u32 oot3d_scene_cutscene_intro_camera_cutscene_row_count;

const Oot3dSceneCutsceneIntroCameraTimelineRow* Oot3d_CutsceneIntroCameraFindTimelineRow(
    u16 cutsceneSourceIndex,
    s32 frame
);

Oot3dCutsceneIntroCameraStatus Oot3d_CutsceneIntroCameraBuildView(
    u16 cutsceneSourceIndex,
    float frameTime,
    const Oot3dCutsceneCameraViewPerturbation* perturbation,
    Oot3dCutsceneCameraViewFrame* outView
);

#endif
