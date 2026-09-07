#pragma once

#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"

Camera InitialCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                     const LinkInstance& link);
void UpdateNativeOot3dCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                             const LinkInstance& link, Camera& camera, double dt);
