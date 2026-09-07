#ifndef OOT3D_SCENE_RESOURCE_H
#define OOT3D_SCENE_RESOURCE_H

#include "oot3d/types.h"

enum {
    OOT3D_SCENE_RESOURCE_COPY_FUNCTION = 0x002EAFB4,
    OOT3D_SCENE_RESOURCE_RUNTIME_TABLE_ADDRESS = 0x00545484,
    OOT3D_SCENE_RESOURCE_SELECTED_NORMAL_SOURCE_ADDRESS = 0x004DC400,
    OOT3D_SCENE_RESOURCE_SELECTED_ALTERNATE_SOURCE_ADDRESS = 0x004DCBA8,
    OOT3D_SCENE_RESOURCE_STATIC_TAIL_TABLE_ADDRESS = 0x00545C2C,
    OOT3D_SCENE_RESOURCE_ENTRY_SIZE = 0x8C,
    OOT3D_SCENE_RESOURCE_METADATA_OFFSET = 0x88,
    OOT3D_SCENE_RESOURCE_SELECTED_COPY_SIZE = 0x000007A8,
    OOT3D_SCENE_RESOURCE_SCENE_ROW_COUNT = 111,
    OOT3D_SCENE_RESOURCE_VARIANT_ROW_COUNT = 14,
};

typedef enum {
    OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_NORMAL,
    OOT3D_SCENE_RESOURCE_BLOCK_STATIC_TAIL,
    OOT3D_SCENE_RESOURCE_BLOCK_SELECTED_ALTERNATE,
} Oot3dSceneResourceBlock;

typedef enum {
    OOT3D_SCENE_RESOURCE_NO_ZSI_PATH,
    OOT3D_SCENE_RESOURCE_NATIVE_ZSI_INDEXED,
    OOT3D_SCENE_RESOURCE_NATIVE_ZSI_NOT_INDEXED,
} Oot3dSceneResourceCoverageStatus;

typedef struct {
    u8 sceneId;
    Oot3dSceneResourceBlock sourceBlock;
    u16 recordIndex;
    u32 recordAddress;
    u32 runtimeAddress;
    u32 metadataAddress;
    u32 runtimeMetadataAddress;
    u32 metadataRaw;
    u8 metadataBytes[4];
    const char* zsiPath;
    const char* zarPath;
    const char* nativeSceneSourceBasename;
    const char* nativeSceneIndexSymbol;
    const char* secondarySceneStem;
    const char* secondarySceneEnum;
    Oot3dSceneResourceCoverageStatus coverageStatus;
} Oot3dSceneResourceRow;

typedef struct {
    u8 variantIndex;
    u8 sceneId;
    Oot3dSceneResourceBlock sourceBlock;
    u16 recordIndex;
    u32 recordAddress;
    u32 runtimeAddress;
    u32 metadataAddress;
    u32 runtimeMetadataAddress;
    u32 metadataRaw;
    u8 metadataBytes[4];
    const char* zsiPath;
    const char* zarPath;
    const char* nativeSceneSourceBasename;
    const char* nativeSceneIndexSymbol;
    Oot3dSceneResourceCoverageStatus coverageStatus;
} Oot3dSceneResourceVariantRow;

extern const Oot3dSceneResourceRow oot3d_scene_resource_rows[];
extern const u32 oot3d_scene_resource_row_count;
extern const Oot3dSceneResourceVariantRow oot3d_scene_resource_variant_rows[];
extern const u32 oot3d_scene_resource_variant_row_count;

#endif
