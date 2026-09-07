#pragma once

#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"

bool ApplyNativeBgCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                         const LinkInstance& link, int cameraDataIndex,
                         const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& bgCamera, Camera& camera, double dt);
