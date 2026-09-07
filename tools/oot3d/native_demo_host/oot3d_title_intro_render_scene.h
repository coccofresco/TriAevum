#pragma once

#include "oot3d_demo_host_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

bool NativePicaLightingDebugRenderMode(const Args& args);

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame);

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment);

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForTitleIntroPlayback(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame);

void ReapplyRenderModeAfterLinkPoseUpdate(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr);

void RebuildRenderSceneBounds(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                              bool includeLink);
void HideGameplayLinkForTitleIntro(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                                   bool rebuildBounds = true);
