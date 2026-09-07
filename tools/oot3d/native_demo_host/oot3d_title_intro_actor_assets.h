#pragma once

#include <filesystem>
#include <string>

#include "oot3d_title_intro_runtime_types.h"

extern "C" {
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_source_table.h"
}

void AddTitleIntroNativeActorMotionClip(TitleIntroNativeActor& actor,
                                        const char* role,
                                        const char* csabName,
                                        uint16_t animationIndex,
                                        float playSpeedScale,
                                        const char* source);
void LoadTitleIntroNativeActorMotionClips(TitleIntroNativeActor& actor,
                                          const Oot3dTitleIntroActorMotionAnimationRow* motionRow);
TitleIntroNativeActor LoadTitleIntroNativeActor(const std::filesystem::path& romfsRoot,
                                                const Oot3dTitleIntroActorAnimationRow* row,
                                                const Oot3dTitleIntroActorMotionAnimationRow* motionRow,
                                                const Oot3dTitleIntroOpeningActorBindingRow* openingBindingRow = nullptr,
                                                const std::string& openingBindingStatus = "",
                                                const Oot3dSceneCutsceneActorResource* resource = nullptr);
TitleIntroNativeActor LoadTitleIntroNativeActorFromCutsceneResource(
    const std::filesystem::path& romfsRoot,
    const Oot3dSceneCutsceneActorResource& resource);
TitleIntroNativeActor LoadTitleIntroNativeActorFromOpeningBinding(
    const std::filesystem::path& romfsRoot,
    const Oot3dTitleIntroOpeningActorBindingRow* binding,
    const std::string& bindingStatus);
void ApplyTitleIntroNativeActorScale(TitleIntroNativeActor& actor,
                                     const Oot3dTitleIntroActorScaleRow* row,
                                     double fallbackCmbToSceneScale);
const TitleIntroNativeActorCsabClip* FindTitleIntroNativeActorMotionClip(
    const TitleIntroNativeActor& actor,
    const char* role);
const TitleIntroNativeActorCsabClip* FindTitleIntroNativeActorMotionClipByCsabName(
    const TitleIntroNativeActor& actor,
    const char* csabName);
TitleIntroActorClipSelection SelectTitleIntroActorClip(
    const TitleIntroNativeActor& actor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion,
    const Oot3dSceneCutsceneActorSnapshot* genericActorSnapshot = nullptr);
double TitleIntroMountedVisualYOffset(const TitleIntroNativeActor& actor,
                                      const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
                                      bool usePairedMountMotion);
ThreeDsRecomp::Oot3d::Oot3dDemoVec3 TitleIntroActorDrawPosition(
    const TitleIntroNativeActor& actor,
    const TitleIntroCueSample& cue,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion);
TitleIntroActorRenderScale ResolveTitleIntroActorRenderScale(
    const TitleIntroNativeActor& actor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion);
bool TitleIntroNativePairedMountDrawOwnsRider(
    const TitleIntroNativeActor& mountActor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    const TitleIntroActorVisualAppendResult& mountVisual);
int FindTitleIntroBoneIndexForCsabNode(const ThreeDsRecomp::Oot3d::CsabMetadata& csab,
                                       uint16_t nodeIndex);
