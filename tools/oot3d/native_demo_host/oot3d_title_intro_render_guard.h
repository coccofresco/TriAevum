#pragma once

#include <nlohmann/json.hpp>

#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

nlohmann::json BuildTitleIntroRenderGuard(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const TitleIntroPlayback& titleIntro);
