#ifndef OOT3D_SCENE_CUTSCENE_SOURCE_TABLE_H
#define OOT3D_SCENE_CUTSCENE_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {
    OOT3D_SCENE_CUTSCENE_SOURCE_ROW_COUNT = 112,
    OOT3D_SCENE_CUTSCENE_HEADER_CANDIDATE_ROW_COUNT = 1436,
    OOT3D_SCENE_CUTSCENE_TIMELINE_COMMAND_ROW_COUNT = 36,
    OOT3D_SCENE_CUTSCENE_CAMERA_POINT_ROW_COUNT = 253,
    OOT3D_SCENE_CUTSCENE_ENTRY_ROW_COUNT = 9,
    OOT3D_SCENE_CUTSCENE_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_CUTSCENE_SOURCE_NO_OFFSET = 0xFFFFFFFF,
};

typedef enum {
    OOT3D_CUTSCENE_DECODE_STRICT_N64_COMPATIBLE,
    OOT3D_CUTSCENE_DECODE_HEADER_CANDIDATES_SEMANTIC_PENDING,
    OOT3D_CUTSCENE_DECODE_UNDECODED_SEMANTIC_PENDING,
} Oot3dCutsceneDecodeStatus;

typedef struct {
    u16 cutsceneSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 settingSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u32 commandOffset;
    u32 commandArgument;
    u8 argumentInBounds;
    u32 bytesAvailableFromArgument;
    u16 headerCandidateRefStart;
    u16 headerCandidateRefCount;
    u16 plausibleHeaderCandidateCount;
    u16 timelineCommandRefStart;
    u16 timelineCommandRefCount;
    u16 cameraPointRefStart;
    u16 cameraPointRefCount;
    u16 entryRefStart;
    u16 entryRefCount;
    Oot3dCutsceneDecodeStatus decodeStatus;
    u32 strictHeaderOffset;
    u32 strictDelta;
    s32 strictTotalEntries;
    s32 strictEndFrame;
    u32 strictDecodedSize;
    u8 strictTerminatedByEnd;
    u16 strictAlternateDecodeCount;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* setupSymbol;
    const char* payloadSymbol;
    const char* rawPrefixHex;
    const char* validationStatus;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
    u32 handlerSetterEntry;
    u32 handlerGetterEntry;
    u32 playCutscenePtrOffset;
    u32 playCutsceneStateOffset;
} Oot3dSceneCutsceneSourceRow;

typedef struct {
    u16 headerCandidateSourceIndex;
    u16 cutsceneSourceIndex;
    u16 localCandidateIndex;
    u32 delta;
    u32 offset;
    s32 totalEntries;
    s32 endFrame;
    u8 plausibleN64Header;
    s32 firstCommandId;
    const char* firstCommandName;
} Oot3dSceneCutsceneHeaderCandidateRow;

typedef struct {
    u16 timelineCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 localCommandIndex;
    u32 commandOffset;
    s32 commandId;
    const char* commandName;
    const char* category;
    u16 entryCount;
    u16 cameraPointCount;
    u32 payloadSize;
    u32 totalSize;
    u32 listHeaderOffset;
    u32 listHeaderWord0;
    u32 listHeaderWord1;
    s32 listHeaderParam;
    s32 listHeaderStartFrame;
    s32 listHeaderEndFrame;
    s32 listHeaderUnused;
    u16 cameraPointRefStart;
    u16 cameraPointRefCount;
    u16 entryRefStart;
    u16 entryRefCount;
} Oot3dSceneCutsceneTimelineCommandRow;

typedef struct {
    u16 cameraPointSourceIndex;
    u16 cutsceneSourceIndex;
    u16 timelineCommandSourceIndex;
    u16 localPointIndex;
    u32 pointOffset;
    s8 continueFlag;
    s8 cameraRoll;
    u16 nextPointFrame;
    u32 viewAngleBits;
    Oot3dVec3s pos;
    s16 unused;
} Oot3dSceneCutsceneCameraPointRow;

typedef struct {
    u16 entrySourceIndex;
    u16 cutsceneSourceIndex;
    u16 timelineCommandSourceIndex;
    u16 localEntryIndex;
    u32 entryOffset;
    u16 rawWordCount;
    u32 rawWords[12];
    u32 primary;
    s32 startFrame;
    s32 endFrame;
} Oot3dSceneCutsceneEntryRow;

extern const Oot3dSceneCutsceneSourceRow oot3d_scene_cutscene_source_rows[];
extern const Oot3dSceneCutsceneHeaderCandidateRow oot3d_scene_cutscene_header_candidate_rows[];
extern const Oot3dSceneCutsceneTimelineCommandRow oot3d_scene_cutscene_timeline_command_rows[];
extern const Oot3dSceneCutsceneCameraPointRow oot3d_scene_cutscene_camera_point_rows[];
extern const Oot3dSceneCutsceneEntryRow oot3d_scene_cutscene_entry_rows[];
extern const u32 oot3d_scene_cutscene_source_row_count;
extern const u32 oot3d_scene_cutscene_header_candidate_row_count;
extern const u32 oot3d_scene_cutscene_timeline_command_row_count;
extern const u32 oot3d_scene_cutscene_camera_point_row_count;
extern const u32 oot3d_scene_cutscene_entry_row_count;

#endif
