#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_TABLE_H
#define OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_TABLE_H

#include "oot3d/scene_cutscene_camera_cmad_table.h"

enum {
    OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_ROW_COUNT = 26148,
    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_KEYFRAME_REF_ROW_COUNT = OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_ROW_COUNT,
    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_ROW_COUNT = 108,
    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_LABEL_ROW_COUNT = 563,
    OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_RECORD_SIZE = 0x10,
    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_TYPE_HERMITE = 2,
    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_MAGIC = 0x74727473,
};

typedef struct {
    u16 keyframeSourceIndex;
    u16 cameraCurveSourceIndex;
    u16 pointIndex;
    u32 keyframeAbsoluteOffset;
    s32 frame;
    u32 valueBits;
    u32 tangentInBits;
    u32 tangentOutBits;
} Oot3dSceneCutsceneCameraKeyframeRow;

typedef struct {
    u16 strtSourceIndex;
    u16 cameraCurveSourceIndex;
    u16 cmadRecordSourceIndex;
    u16 labelRefStart;
    u16 labelRefCount;
    u32 strtAbsoluteOffset;
    u32 strtSize;
    u32 curveDeclaredEndOffset;
    u32 cmadDeclaredEndOffset;
    u8 extendsPastCurveSize;
    u8 extendsPastCmadSize;
    const char* scenePath;
    const char* curveRole;
    const char* rawStrtHex;
} Oot3dSceneCutsceneCameraStrtRow;

typedef struct {
    u16 strtLabelSourceIndex;
    u16 strtSourceIndex;
    u16 cameraCurveSourceIndex;
    u16 labelIndex;
    u32 labelStart;
    u32 labelEnd;
    u8 parsedLabel;
    u16 parsedCameraIndex;
    s32 parsedStartFrame;
    s32 parsedEndFrame;
    const char* label;
} Oot3dSceneCutsceneCameraStrtLabelRow;

extern const Oot3dSceneCutsceneCameraKeyframeRow oot3d_scene_cutscene_camera_keyframe_rows[];
extern const u32 oot3d_scene_cutscene_camera_curve_keyframe_ref_start[];
extern const u16 oot3d_scene_cutscene_camera_curve_keyframe_ref_count[];
extern const Oot3dSceneCutsceneCameraStrtRow oot3d_scene_cutscene_camera_strt_rows[];
extern const Oot3dSceneCutsceneCameraStrtLabelRow oot3d_scene_cutscene_camera_strt_label_rows[];
extern const u32 oot3d_scene_cutscene_camera_keyframe_row_count;
extern const u32 oot3d_scene_cutscene_camera_strt_row_count;
extern const u32 oot3d_scene_cutscene_camera_strt_label_row_count;

#endif
