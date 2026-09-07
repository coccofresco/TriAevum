#pragma once

#include <cstdint>
#include <string>
#include <string_view>

extern "C" {
#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
#include "oot3d/title_intro_opening_resolver_runtime.h"
#include "oot3d/title_intro_source_table.h"
}

bool TitleIntroScenePathMatches(const char* lhs, const char* rhs);
const Oot3dTitleIntroOpeningOrchestrationRow* ResolveTitleIntroOpeningOrchestrationFromNativeTable(
    uint16_t& outIndex,
    std::string& outStatus);
const Oot3dSceneCutsceneIntroCameraCutsceneRow* FindTitleIntroInitialSceneCutsceneRowForOpeningOrchestration(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    std::string& outStatus);
int ResolveTitleIntroInitialSceneSetupOverride(bool titleIntroPlaybackEnabled);
std::string_view ResolveTitleIntroInitialSceneSetupSource(bool titleIntroPlaybackEnabled);
const Oot3dTitleIntroOpeningRequiredAssetRef* FindTitleIntroOpeningRequiredAssetRef(
    uint16_t orchestrationIndex,
    uint16_t assetRefIndex);
const Oot3dTitleIntroOpeningActorBindingRow* ResolveTitleIntroOpeningActorBinding(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    uint16_t actorKind,
    std::string& outStatus);
const Oot3dTitleIntroQdbSourceRow* ResolveTitleIntroOpeningQdbRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    bool qdbIndexOverride,
    uint16_t qdbIndexOverrideValue,
    uint16_t& outQdbIndex,
    std::string& outStatus);
const Oot3dSceneCutsceneCameraBlobRow* FindTitleIntroOpeningCameraBlobRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration);
const Oot3dSceneCutsceneCameraBlobSegmentRow* FindTitleIntroOpeningCameraSegmentRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dSceneCutsceneCameraBlobRow* blobRow,
    double frame);
