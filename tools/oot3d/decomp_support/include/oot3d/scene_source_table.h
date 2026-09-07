#ifndef OOT3D_SCENE_SOURCE_TABLE_H
#define OOT3D_SCENE_SOURCE_TABLE_H

#include "oot3d/scene_global_entrance.h"
#include "oot3d/scene_resource.h"

enum {
    OOT3D_SCENE_SOURCE_ROW_COUNT = 111,
    OOT3D_SCENE_SOURCE_VARIANT_ROW_COUNT = 14,
    OOT3D_SCENE_SOURCE_ENTRANCE_REF_COUNT = 135,
};

typedef struct {
    u16 entranceIndex;
    u8 localEntranceIndex;
    u8 referenceFlags;
    Oot3dGlobalEntranceValidationStatus validationStatus;
} Oot3dSceneSourceEntranceRef;

typedef struct {
    u8 sceneId;
    Oot3dSceneResourceBlock resourceBlock;
    u16 resourceRecordIndex;
    const char* zsiPath;
    const char* zarPath;
    const char* sourceBasename;
    const char* sourceCFile;
    const char* sceneIndexSymbol;
    const char* setupSymbol;
    const char* roomRefsSymbol;
    const char* roomsSymbol;
    u16 setupCount;
    u16 roomCount;
    u16 commandCount;
    u16 entranceRefStart;
    u16 entranceRefCount;
} Oot3dSceneSourceRow;

typedef struct {
    u8 variantIndex;
    u8 sceneId;
    Oot3dSceneResourceBlock resourceBlock;
    u16 resourceRecordIndex;
    const char* zsiPath;
    const char* zarPath;
    const char* sourceBasename;
    const char* sourceCFile;
    const char* sceneIndexSymbol;
} Oot3dSceneSourceVariantRow;

extern const Oot3dSceneSourceEntranceRef oot3d_scene_source_entrance_refs[];
extern const Oot3dSceneSourceRow oot3d_scene_source_rows[];
extern const Oot3dSceneSourceVariantRow oot3d_scene_source_variant_rows[];
extern const u32 oot3d_scene_source_row_count;
extern const u32 oot3d_scene_source_variant_row_count;
extern const u32 oot3d_scene_source_entrance_ref_count;

#endif
