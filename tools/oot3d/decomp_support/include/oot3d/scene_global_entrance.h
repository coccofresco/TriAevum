#ifndef OOT3D_SCENE_GLOBAL_ENTRANCE_H
#define OOT3D_SCENE_GLOBAL_ENTRANCE_H

#include "oot3d/types.h"

enum {
    OOT3D_GLOBAL_ENTRANCE_TABLE_ADDRESS = 0x00543BB8,
    OOT3D_GLOBAL_ENTRANCE_ENTRY_SIZE = 4,
    OOT3D_GLOBAL_ENTRANCE_INFERRED_TABLE_ENTRY_COUNT = 1586,
    OOT3D_GLOBAL_ENTRANCE_REFERENCED_ROW_COUNT = 138,
};

typedef enum {
    OOT3D_GLOBAL_ENTRANCE_REF_DIRECT_EXIT = 1 << 0,
    OOT3D_GLOBAL_ENTRANCE_REF_HIGH_REMAP_EXIT = 1 << 1,
    OOT3D_GLOBAL_ENTRANCE_REF_KOKIRI_SLOT5_FIXTURE = 1 << 2,
    OOT3D_GLOBAL_ENTRANCE_REF_CUTSCENE_DIRECT_PLAYER_EVENT = 1 << 3,
} Oot3dGlobalEntranceReferenceFlags;

typedef enum {
    OOT3D_GLOBAL_ENTRANCE_OUTSIDE_CODE_BIN_IMAGE,
    OOT3D_GLOBAL_ENTRANCE_OUTSIDE_INFERRED_TABLE_EXTENT,
    OOT3D_GLOBAL_ENTRANCE_DECODED_SCENE_ID_UNLABELED,
    OOT3D_GLOBAL_ENTRANCE_SECONDARY_LABEL_NO_NATIVE_STEM_MATCH,
    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_UNCOVERED,
    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_COVERED,
    OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_SPAWN_RESOLVED,
    OOT3D_GLOBAL_ENTRANCE_NATIVE_KOKIRI_SLOT5_ENTRYPOINT_CONFIRMED,
} Oot3dGlobalEntranceValidationStatus;

typedef struct {
    u16 entranceIndex;
    u8 referenceFlags;
    u8 sceneId;
    u8 localEntranceIndex;
    u16 field;
    const char* secondarySceneStem;
    const char* secondarySceneEnum;
    const char* nativeScenePathCandidate;
    const char* nativeSceneResourceZsiPath;
    const char* nativeSceneSourceBasename;
    const char* nativeSceneIndexSymbol;
    Oot3dGlobalEntranceValidationStatus validationStatus;
} Oot3dGlobalEntranceRow;

extern const Oot3dGlobalEntranceRow oot3d_global_entrance_referenced_rows[];
extern const u32 oot3d_global_entrance_referenced_row_count;

const Oot3dGlobalEntranceRow* Oot3d_GlobalEntranceFindRow(
    u16 entranceIndex
);

#endif
