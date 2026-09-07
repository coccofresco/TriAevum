#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_APPLY_H
#define OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_APPLY_H

#include "oot3d/scene_cutscene_camera_curve_eval.h"

typedef struct {
    float csParam80;
    float csParam84;
    float csParam88;
    float csParam8C;
    float csParam90;
    float csParam94;
    float csParam144;
    float csParamD0;
    s16 csParam1A2;
} Oot3dCutsceneCameraState;

typedef enum {
    OOT3D_CUTSCENE_CAMERA_APPLY_OK = 0,
    OOT3D_CUTSCENE_CAMERA_APPLY_NULL_STATE,
    OOT3D_CUTSCENE_CAMERA_APPLY_NULL_SEGMENT_ROW,
    OOT3D_CUTSCENE_CAMERA_APPLY_SEGMENT_INDEX_OUT_OF_RANGE,
    OOT3D_CUTSCENE_CAMERA_APPLY_CMAD_INDEX_OUT_OF_RANGE,
    OOT3D_CUTSCENE_CAMERA_APPLY_CURVE_SLICE_OUT_OF_RANGE,
    OOT3D_CUTSCENE_CAMERA_APPLY_CURVE_OWNER_MISMATCH,
    OOT3D_CUTSCENE_CAMERA_APPLY_SAMPLE_FAILED,
    OOT3D_CUTSCENE_CAMERA_APPLY_UNSUPPORTED_OUTPUT_FIELD,
} Oot3dCutsceneCameraApplyStatus;

Oot3dCutsceneCameraApplyStatus Oot3d_CutsceneCameraInitStateFromSegmentRow(
    const Oot3dSceneCutsceneCameraBlobSegmentRow* segment,
    Oot3dCutsceneCameraState* state
);

Oot3dCutsceneCameraApplyStatus Oot3d_CutsceneCameraInitSegmentState(
    u16 cameraBlobSegmentSourceIndex,
    Oot3dCutsceneCameraState* state
);

Oot3dCutsceneCameraApplyStatus Oot3d_CutsceneCameraApplyCmadRecord(
    u16 cmadRecordSourceIndex,
    float frame,
    Oot3dCutsceneCameraState* state
);

Oot3dCutsceneCameraApplyStatus Oot3d_CutsceneCameraApplySegmentCmad(
    u16 cameraBlobSegmentSourceIndex,
    float frame,
    Oot3dCutsceneCameraState* state
);

Oot3dCutsceneCameraApplyStatus Oot3d_CutsceneCameraBuildSegmentState(
    u16 cameraBlobSegmentSourceIndex,
    float frame,
    Oot3dCutsceneCameraState* state
);

#endif
