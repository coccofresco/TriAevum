#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_TABLE_H
#define OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_cutscene_camera_blob_table.h"

enum {
    OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_RECORD_ROW_COUNT = 1374,
    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_ROW_COUNT = 3566,
    OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_MAGIC = 0x64616D63,
};

typedef struct {
    u16 cmadRecordSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 segmentIndex;
    u16 cmadRecordIndex;
    u32 madsRecordOffset;
    u32 cmadAbsoluteOffset;
    u32 cmadRecordSize;
    u32 nextCmadOffset;
    u32 cmadMagic;
    u8 channelType;
    u16 curveRefStart;
    u16 curveRefCount;
    const char* scenePath;
    const char* rawCmadHeaderPrefixHex;
} Oot3dSceneCutsceneCameraCmadRecordRow;

typedef struct {
    u16 cameraCurveSourceIndex;
    u16 cmadRecordSourceIndex;
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 segmentIndex;
    u16 cmadRecordIndex;
    u8 channelType;
    u8 curveSlotIndex;
    u32 outputFieldOffset;
    s32 curveOffset;
    u32 curveAbsoluteOffset;
    u32 curveSize;
    u8 interpolationType;
    u16 pointCount;
    u32 headerWord08;
    u32 headerWord0C;
    const char* curveRole;
    const char* scenePath;
    const char* rawCurveHeaderPrefixHex;
} Oot3dSceneCutsceneCameraCurveRow;

extern const Oot3dSceneCutsceneCameraCmadRecordRow oot3d_scene_cutscene_camera_cmad_record_rows[];
extern const Oot3dSceneCutsceneCameraCurveRow oot3d_scene_cutscene_camera_curve_rows[];
extern const u32 oot3d_scene_cutscene_camera_cmad_record_row_count;
extern const u32 oot3d_scene_cutscene_camera_curve_row_count;

#endif
