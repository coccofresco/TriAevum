#pragma once

#include <string>

#include "oot3d_title_intro_runtime_types.h"

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput TitleIntroRuntimeEnvironmentInput(
    const TitleIntroPlayback& playback);
ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput CutsceneFrameRuntimeEnvironmentInput(
    const Oot3dSceneCutsceneFrameSnapshot& snapshot);
const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* RuntimeEnvironmentInputPtr(
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input);
bool TitleIntroRuntimeEnvironmentNeedsRenderRebuild(
    const TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input,
    float materialAnimationFrame);
void MarkTitleIntroRuntimeEnvironmentRendered(
    TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input,
    float materialAnimationFrame,
    std::string materialAnimationFrameSource,
    bool rebuilt);
