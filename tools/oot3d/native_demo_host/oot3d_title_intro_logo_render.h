#pragma once

#include "oot3d_demo_host_types.h"
#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

void ApplyTitleIntroOpeningFrameRuntimeLogoDraw(
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const Camera& camera,
    double aspect);
