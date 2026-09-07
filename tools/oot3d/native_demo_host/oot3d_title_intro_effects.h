#pragma once

#include "oot3d_demo_host_types.h"
#include "oot3d_title_intro_runtime_types.h"

void InitializeTitleIntroNativeEffects(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback);
void UpdateAndAppendTitleIntroNativeEffects(
    TitleIntroPlayback& playback,
    const TitleIntroActorVisualAppendResult& eponaVisual,
    const Camera& camera,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
