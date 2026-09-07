#pragma once

#include <filesystem>
#include <vector>

#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

extern "C" {
#include "oot3d/title_intro_opening_logo_runtime.h"
#include "oot3d/title_intro_opening_orchestration.h"
}

TitleIntroLogoRuntime LoadTitleIntroLogoRuntime(
    const std::filesystem::path& romfsRoot,
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dTitleIntroOpeningActorBindingRow* logoBinding,
    const std::string& logoBindingStatus);
TitleIntroLogoAlphaState SimulateTitleIntroLogoAlphaState(const TitleIntroLogoRuntime& runtime,
                                                          double titleFrame);
TitleIntroLogoAlphaState TitleIntroLogoAlphaStateFromOpeningFrameRuntime(
    const TitleIntroLogoRuntime& runtime,
    const Oot3dTitleIntroOpeningLogoState& logoState,
    const Oot3dTitleIntroOpeningLogoStepResult& logoStep,
    double titleFrame);
std::vector<TitleIntroLogoAlphaState> BuildTitleIntroLogoAlphaReferenceSamples(
    const TitleIntroLogoRuntime& runtime);
bool IsTitleIntroLogoRenderModel(const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& model);
