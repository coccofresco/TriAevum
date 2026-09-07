#pragma once

#include <cstddef>
#include <functional>

#include "oot3d_demo_host_types.h"
#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

using TitleIntroRenderSceneBuilder = std::function<ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame)>;

void ApplyTitleIntroPlaybackToRenderScene(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    Camera& camera,
    size_t baseActorVisualCount,
    double aspect,
    const TitleIntroRenderSceneBuilder& buildRenderSceneForMode,
    bool buildActorDiagnostics = true);

void SetTitleIntroPlaybackFrame(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    Camera& camera,
    size_t baseActorVisualCount,
    double aspect,
    double frame,
    const TitleIntroRenderSceneBuilder& buildRenderSceneForMode,
    bool buildActorDiagnostics = true);
