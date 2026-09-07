#ifndef OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_TABLE_H
#define OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_cutscene_source_table.h"

enum {
    OOT3D_SCENE_CUTSCENE_NATIVE_SOURCE_ROW_COUNT = 112,
    OOT3D_SCENE_CUTSCENE_NATIVE_COMMAND_SOURCE_ROW_COUNT = 1154,
    OOT3D_SCENE_CUTSCENE_NATIVE_NO_OFFSET = 0xFFFFFFFF,
    OOT3D_SCENE_CUTSCENE_NATIVE_MAGIC_QDB = 0x51444220,
};

typedef enum {
    OOT3D_CUTSCENE_NATIVE_DECODED_QDB,
    OOT3D_CUTSCENE_NATIVE_UNDECODED_STUB_OR_NO_QDB_MAGIC,
} Oot3dCutsceneNativeDecodeStatus;

typedef enum {
    OOT3D_CUTSCENE_NATIVE_COMMAND_UNKNOWN,
    OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_U32_SIZE,
    OOT3D_CUTSCENE_NATIVE_COMMAND_BLOB_16BIT_COUNT,
    OOT3D_CUTSCENE_NATIVE_COMMAND_CAMERA_LIST,
    OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_12WORD_ENTRIES,
    OOT3D_CUTSCENE_NATIVE_COMMAND_COUNTED_3WORD_ENTRIES,
    OOT3D_CUTSCENE_NATIVE_COMMAND_FIXED16,
    OOT3D_CUTSCENE_NATIVE_COMMAND_NOOP,
    OOT3D_CUTSCENE_NATIVE_COMMAND_PACKED_3WORD_PAIRS,
    OOT3D_CUTSCENE_NATIVE_COMMAND_SINGLE_CAMERA_FIXED,
} Oot3dCutsceneNativeCommandCategory;

typedef struct {
    u16 cutsceneSourceIndex;
    u8 sceneId;
    u16 setupIndex;
    u32 commandArgument;
    Oot3dCutsceneNativeDecodeStatus decodeStatus;
    u8 nativeDecoded;
    u32 nativeHeaderDelta;
    u32 nativeHeaderOffset;
    u32 nativeMagic;
    u32 nativeVersionOrFlags;
    u16 nativeCommandCount;
    s32 nativeEndFrame;
    u32 nativeDecodedSize;
    u16 nativeCommandRefStart;
    u16 nativeCommandRefCount;
    u8 strictDecoded;
    const char* scenePath;
    const char* sceneStem;
    const char* setupSymbol;
    const char* payloadSymbol;
    const char* nativeDecodeErrorSample;
} Oot3dSceneCutsceneNativeSourceRow;

typedef struct {
    u16 nativeCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 setupIndex;
    u16 localCommandIndex;
    u32 commandOffset;
    s32 commandId;
    Oot3dCutsceneNativeCommandCategory category;
    u16 entryCount;
    u16 cameraPointCount;
    u32 blobSize;
    u32 packedStride;
    u32 payloadSize;
    u32 totalSize;
    const char* commandName;
    const char* categoryName;
    const char* semanticKind;
    const char* rawPrefixHex;
} Oot3dSceneCutsceneNativeCommandSourceRow;

extern const Oot3dSceneCutsceneNativeSourceRow oot3d_scene_cutscene_native_source_rows[];
extern const Oot3dSceneCutsceneNativeCommandSourceRow oot3d_scene_cutscene_native_command_source_rows[];
extern const u32 oot3d_scene_cutscene_native_source_row_count;
extern const u32 oot3d_scene_cutscene_native_command_source_row_count;

#endif
