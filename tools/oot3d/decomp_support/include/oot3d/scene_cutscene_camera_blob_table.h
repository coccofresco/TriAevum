#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_TABLE_H
#define OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_cutscene_native_source_table.h"

enum {
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_ROW_COUNT = 109,
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_SEGMENT_ROW_COUNT = 564,
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CCB = 0x00626363,
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CAAD = 0x64616163,
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_MADS = 0x7364616D,
    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CMAD = 0x64616D63,
};

typedef struct {
    u16 cameraBlobSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u8 sceneId;
    u16 setupIndex;
    u16 localCommandIndex;
    u32 commandOffset;
    u32 commandBlobSize;
    u32 blobDataOffset;
    u32 blobMagic;
    u32 blobVersion;
    u32 logicalBlobSize;
    u32 tailPaddingSize;
    u32 reservedWord0C;
    u16 segmentCount;
    u32 headerWord14;
    u32 segmentDirectoryEndOffset;
    u16 segmentRefStart;
    u16 segmentRefCount;
    u32 firstSegmentOffset;
    const char* scenePath;
    const char* sceneStem;
    const char* payloadSymbol;
    const char* rawBlobHeaderPrefixHex;
} Oot3dSceneCutsceneCameraBlobRow;

typedef struct {
    u16 cameraBlobSegmentSourceIndex;
    u16 cameraBlobSourceIndex;
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u16 segmentIndex;
    u32 segmentOffset;
    u32 segmentAbsoluteOffset;
    u32 segmentSize;
    u32 nextSegmentOffset;
    u32 segmentMagic;
    u32 segmentWord04;
    s32 startFrame;
    s32 endFrame;
    u32 baseCsParam8CBits;
    u32 baseCsParam90Bits;
    u32 baseCsParam94Bits;
    u32 baseCsParam80Bits;
    u32 baseCsParam84Bits;
    u32 baseCsParam88Bits;
    u32 baseCsParam1A2SourceBits;
    u32 baseCsParam144SourceBits;
    u32 baseCsParamD0Bits;
    u32 madsOffset;
    u32 madsSize;
    u32 madsMagic;
    u32 madsType;
    u32 madsHeaderSize;
    u32 cmadOffset;
    u32 cmadMagic;
    const char* scenePath;
    const char* rawSegmentHeaderPrefixHex;
} Oot3dSceneCutsceneCameraBlobSegmentRow;

extern const Oot3dSceneCutsceneCameraBlobRow oot3d_scene_cutscene_camera_blob_rows[];
extern const Oot3dSceneCutsceneCameraBlobSegmentRow oot3d_scene_cutscene_camera_blob_segment_rows[];
extern const u32 oot3d_scene_cutscene_camera_blob_row_count;
extern const u32 oot3d_scene_cutscene_camera_blob_segment_row_count;

#endif
