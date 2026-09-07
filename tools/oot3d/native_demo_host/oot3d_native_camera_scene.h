#pragma once

#include <vector>

#include "oot3d_native_camera_config.h"

const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* NativeBgCameraForDataIndex(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, int cameraDataIndex);
bool NativeBgCameraHasActiveSetting(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* bgCamera);
bool NativeCameraSettingListed(const std::vector<int>& settings, int setting);
int NativeFloorNoneDefaultCameraDataIndex(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                          const NativeCameraConfig& config);
bool NativeStartCameraYieldsToFloorNoneDefault(const NativeCameraConfig& config,
                                               const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* startBgCamera);
int NativeStartCameraDataIndex(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                               const NativeCameraConfig& config);
const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCameraPosition* NativeBgCameraPositionForCamera(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& camera);
const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCameraPosition* NativeBgCameraPositionForCameraGroup(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& camera,
    int groupOffset);
