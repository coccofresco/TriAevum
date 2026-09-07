#pragma once

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_types.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_native_camera_config.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

nlohmann::json BaseSummary(const Args& args, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                           const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                           const LinkNativeLocomotionConfig& locomotionConfig,
                           const NativeCameraConfig& cameraConfig);
