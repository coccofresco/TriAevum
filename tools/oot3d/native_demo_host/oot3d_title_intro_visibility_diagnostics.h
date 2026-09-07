#pragma once

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

nlohmann::json TitleIntroVisibilityDiagnosticsToJson(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const Camera& camera,
    uint32_t width,
    uint32_t height);
