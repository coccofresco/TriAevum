#include "oot3d_native_camera_scene.h"

#include <algorithm>
#include <cstddef>

const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* NativeBgCameraForDataIndex(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, int cameraDataIndex) {
    if (cameraDataIndex < 0 || static_cast<size_t>(cameraDataIndex) >= scene.Collision.BgCameras.size()) {
        return nullptr;
    }
    return &scene.Collision.BgCameras[static_cast<size_t>(cameraDataIndex)];
}

bool NativeBgCameraHasActiveSetting(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* bgCamera) {
    return bgCamera != nullptr && bgCamera->Setting != kNativeCameraSetNone;
}

bool NativeCameraSettingListed(const std::vector<int>& settings, int setting) {
    return std::find(settings.begin(), settings.end(), setting) != settings.end();
}

int NativeFloorNoneDefaultCameraDataIndex(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                          const NativeCameraConfig& config) {
    if (!config.FloorNoneDefaultCameraSelectorSupported) {
        return -1;
    }

    for (const int preferredSetting : config.FloorNoneDefaultCameraSettings) {
        for (size_t index = 0; index < scene.Collision.BgCameras.size(); ++index) {
            const auto& bgCamera = scene.Collision.BgCameras[index];
            if (bgCamera.Setting != preferredSetting || !NativeBgCameraHasActiveSetting(&bgCamera) ||
                NativeBgCameraPositionForCamera(scene, bgCamera) == nullptr) {
                continue;
            }
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool NativeStartCameraYieldsToFloorNoneDefault(const NativeCameraConfig& config,
                                               const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera* startBgCamera) {
    return startBgCamera == nullptr ||
           !NativeBgCameraHasActiveSetting(startBgCamera) ||
           NativeCameraSettingListed(config.FloorNoneDefaultStartSettingsThatYield, startBgCamera->Setting);
}

int NativeStartCameraDataIndex(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                               const NativeCameraConfig& config) {
    if (!config.StartCameraDataSupported || scene.PlayerStart.CameraDataIndex == kNativeCameraIgnoreSentinel) {
        return -1;
    }
    return scene.PlayerStart.CameraDataIndex;
}

const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCameraPosition* NativeBgCameraPositionForCamera(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& camera) {
    if (camera.Count < 3 || scene.Collision.BgCameraPositions.empty()) {
        return nullptr;
    }
    int groupIndex = camera.CameraPositionVectorIndex;
    if (groupIndex >= 0 && groupIndex % 3 == 0) {
        groupIndex /= 3;
    }
    if (groupIndex < 0 || static_cast<size_t>(groupIndex) >= scene.Collision.BgCameraPositions.size()) {
        return nullptr;
    }
    return &scene.Collision.BgCameraPositions[static_cast<size_t>(groupIndex)];
}

const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCameraPosition* NativeBgCameraPositionForCameraGroup(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoBgCamera& camera,
    int groupOffset) {
    if (camera.Count < 3 || scene.Collision.BgCameraPositions.empty()) {
        return nullptr;
    }
    int groupIndex = camera.CameraPositionVectorIndex;
    if (groupIndex >= 0 && groupIndex % 3 == 0) {
        groupIndex /= 3;
    }
    groupIndex += groupOffset;
    if (groupIndex < 0 || static_cast<size_t>(groupIndex) >= scene.Collision.BgCameraPositions.size()) {
        return nullptr;
    }
    return &scene.Collision.BgCameraPositions[static_cast<size_t>(groupIndex)];
}
