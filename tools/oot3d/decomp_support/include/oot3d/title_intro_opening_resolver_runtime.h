#ifndef OOT3D_TITLE_INTRO_OPENING_RESOLVER_RUNTIME_H
#define OOT3D_TITLE_INTRO_OPENING_RESOLVER_RUNTIME_H

#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/title_intro_source_table.h"
#include "oot3d/types.h"

const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningResolverFindNativeOrchestration(
    u16* outIndex,
    const char** outStatus
);
const Oot3dSceneCutsceneIntroCameraCutsceneRow*
Oot3d_TitleIntroOpeningResolverFindInitialSceneCutsceneRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const char** outStatus
);
const Oot3dTitleIntroOpeningRequiredAssetRef* Oot3d_TitleIntroOpeningResolverFindRequiredAssetRef(
    u16 orchestrationIndex,
    u16 assetRefIndex
);
const Oot3dTitleIntroOpeningActorBindingRow* Oot3d_TitleIntroOpeningResolverFindActorBinding(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    u16 actorKind,
    const char** outStatus
);
u8 Oot3d_TitleIntroOpeningResolverStatusIsRequiredAssetNotPresent(const char* status);
u8 Oot3d_TitleIntroOpeningResolverStatusIsMissingActorBindingForKind(const char* status);
const Oot3dTitleIntroQdbSourceRow* Oot3d_TitleIntroOpeningResolverFindQdbRow(
    u16 qdbIndex
);
const Oot3dTitleIntroQdbSourceRow* Oot3d_TitleIntroOpeningResolverResolveQdbRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    u16* outQdbIndex,
    const char** outStatus
);
const char* Oot3d_TitleIntroOpeningResolverQdbOverrideStatus(u8 rowFound);
u8 Oot3d_TitleIntroOpeningResolverScenePathMatches(
    const char* lhs,
    const char* rhs
);
const Oot3dSceneCutsceneCameraBlobRow* Oot3d_TitleIntroOpeningResolverFindCameraBlobRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration
);
const Oot3dSceneCutsceneCameraBlobSegmentRow* Oot3d_TitleIntroOpeningResolverFindCameraSegmentRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dSceneCutsceneCameraBlobRow* blobRow,
    float frame
);
const char* Oot3d_TitleIntroOpeningResolverInitialSceneSetupSource(u8 titleIntroPlaybackEnabled);
const char* Oot3d_TitleIntroOpeningResolverPlaybackStatus(u8 initialized);

#endif
