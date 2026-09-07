#ifndef OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_TABLE_H
#define OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_TABLE_H

#include "oot3d/types.h"

enum {
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_ROW_COUNT = 3,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_CANDIDATE_ROW_COUNT = 30,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_EFFECTIVE_ENTRANCE_ROW_COUNT = 60,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_NO_CUTSCENE_SOURCE_INDEX = 65535,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS = 0x00587958,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_TABLE_ADDRESS = 0x00543BB8,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_ADDRESS = 0x00588958,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET = 0x04E8,
    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_SOURCE_ADDRESS = 0x00588E40,
};

typedef enum {
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNRESOLVED_GLOBAL_ENTRANCE,
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_SCENE_MATCHES,
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_CUTSCENE_CANDIDATES,
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNIQUE_NATIVE_CUTSCENE_CANDIDATE,
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_REQUIRES_SCENE_LAYER_OFFSET,
    OOT3D_CUTSCENE_TRANSITION_HANDOFF_AMBIGUOUS_NATIVE_CUTSCENE_CANDIDATES,
} Oot3dCutsceneTransitionHandoffStatus;

typedef struct {
    u16 effectiveEntranceSourceIndex;
    u16 handoffSourceIndex;
    u16 directPlayerEventSourceIndex;
    u16 transitionRequestIndex;
    u8 sceneLayerOffset;
    u16 effectiveEntranceIndex;
    u8 decodedFromCodeBin;
    u8 sceneId;
    u8 localEntranceIndex;
    u16 field;
    const char* rawHex;
    const char* nativeScenePathCandidate;
    const char* nativeSceneIndexSymbol;
    u16 nativeMatchCount;
    u16 cutsceneCandidateCount;
    const char* decodeStatus;
} Oot3dSceneCutsceneTransitionEffectiveEntranceRow;

typedef struct {
    u16 candidateSourceIndex;
    u16 handoffSourceIndex;
    u16 directPlayerEventSourceIndex;
    u16 transitionRequestIndex;
    u8 sceneId;
    u8 localEntranceIndex;
    u16 field;
    const char* scenePath;
    u16 setupIndex;
    u16 cutsceneSourceIndex;
    s32 nativeEndFrame;
    u16 nativeCommandCount;
    u8 spawnResolved;
    u8 entranceSpawn;
    s8 entranceRoom;
    const char* spawnActorName;
    s16 spawnPos[3];
    s16 spawnRot[3];
    u16 spawnParams;
} Oot3dSceneCutsceneTransitionHandoffCandidateRow;

typedef struct {
    u16 handoffSourceIndex;
    u16 directPlayerEventSourceIndex;
    u16 sourceCutsceneSourceIndex;
    const char* sourceScenePath;
    u16 sourceSetupIndex;
    u16 actionId;
    u16 startFrame;
    u16 transitionRequestIndex;
    u8 transitionRequestTrigger;
    u8 transitionRequestEffect;
    u8 globalEntranceResolved;
    u8 sceneId;
    u8 localEntranceIndex;
    u16 field;
    const char* nativeScenePathCandidate;
    const char* nativeSceneIndexSymbol;
    u16 nativeMatchCount;
    u16 candidateFirstIndex;
    u16 candidateCount;
    u16 cutsceneCandidateCount;
    u8 requiresSceneLayerOffset;
    u16 effectiveEntranceFirstIndex;
    u16 effectiveEntranceCount;
    Oot3dCutsceneTransitionHandoffStatus status;
} Oot3dSceneCutsceneTransitionHandoffRow;

extern const Oot3dSceneCutsceneTransitionHandoffRow oot3d_scene_cutscene_transition_handoff_rows[];
extern const Oot3dSceneCutsceneTransitionHandoffCandidateRow oot3d_scene_cutscene_transition_handoff_candidate_rows[];
extern const Oot3dSceneCutsceneTransitionEffectiveEntranceRow oot3d_scene_cutscene_transition_effective_entrance_rows[];
extern const u32 oot3d_scene_cutscene_transition_handoff_row_count;
extern const u32 oot3d_scene_cutscene_transition_handoff_candidate_row_count;
extern const u32 oot3d_scene_cutscene_transition_effective_entrance_row_count;

const char* Oot3d_CutsceneTransitionHandoffEffectiveEntranceExpression(void);

const Oot3dSceneCutsceneTransitionHandoffRow* Oot3d_CutsceneTransitionHandoffFindByDirectPlayerEvent(
    u16 directPlayerEventSourceIndex
);

#endif
